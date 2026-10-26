#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "app/app.h"
#include "common/menu.h"
#include "common/options.h"
#include "game/layout.h"
#include "xdk/hle.h"

using namespace kernel;
using namespace layout;

namespace {

// character: the player is the one in the world container, its kind 2
constexpr uint32_t kCharKind = 0xC4;
constexpr uint32_t kCharPosition = 0xA0;       // float[3] x, y, z
constexpr uint32_t kCharForward = 0xF0;        // float[3] fx, fy, fz (camera forward vector)
constexpr uint32_t kCharPrevPos = 0x190;       // float[3] px, py, pz
constexpr uint32_t kCharVelocity = 0x1B0;      // float[3] vx, vy, vz
constexpr uint32_t kCharGrenades = 0x286;      // uint16
constexpr uint32_t kCharHealth = 0x2F0;        // float, the difficulty's full at most
constexpr uint32_t kCharDying = 0x384;         // not 0 once it is killed
// byte the scripts set: the player's damage calls (its class's 0x1ED990, 0x1ED700, 0x1ED2B0, 0x1ED5D0, and
// 0x1ED1E0) take nothing while it is
constexpr uint32_t kCharInvulnerable = 0x8F2;
constexpr uint32_t kPlayerKind = 2;

constexpr uint32_t kDifficulty = 0x2014C;  // in the game object, 0 easy to 3 black ops
// full health, as 0x13F10 scales 1.0: easy and normal, then hard and black ops
constexpr float kFullHealth[] = {1200.0f, 750.0f};

uint32_t Player() { return R32(kWorld) ? R32(kWorld) + kWorldPlayer : 0; }

float FullHealth() { return kFullHealth[R32(R32(kGame) + kDifficulty) >= 2]; }

// One-hit kill: a hit of the player's on another character starts from a sliver of health, any damage kills.
// A hit that took nothing gives the health back
constexpr float kSliver = 0.001f;

struct Doomed {
    uint32_t who = 0;
    float health = 0;
};

Doomed Doom(uint32_t victim, uint32_t attacker) {
    uint32_t me = Player();
    if (!game::Opt().one_hit_kill || !me || attacker != me || victim == me || R32(victim + kCharDying)) return {};
    float& health = G<float>(victim + kCharHealth);
    if (!(health > kSliver)) return {};
    Doomed kept{victim, health};
    health = kSliver;
    return kept;
}

void Spare(const Doomed& kept) {
    if (!kept.who || R32(kept.who + kCharDying)) return;
    float& health = G<float>(kept.who + kCharHealth);
    if (health > 0) health = kept.health - (kSliver - health);
}

bool g_invulnerable;  // the flag is ours, cleared when god mode goes off

bool IsShotgun(uint32_t shot, uint32_t weapon) {
    if (shot) {
        // 1. Pellet count: shotguns in Black fire multiple projectiles per shell (e.g. 8-10 pellets)
        uint32_t proj_def = R32(shot + 0x0C);
        if (proj_def && R32(proj_def) > 1) return true;

        // 2. Ammunition type 6: 12 gauge shotgun shells
        uint32_t mag_data = R32(shot + 0x08);
        if (mag_data && R32(mag_data + 0x64) == 6) return true;
    }
    if (weapon) {
        // 3. Weapon manager table lookup for shotgun weapon codes (SHG, RM1, SH5, RM5)
        uint32_t mgr = R32(0x002D12D0);
        uint32_t table = mgr ? R32(mgr) : 0;
        if (table) {
            uint32_t entries = R32(table + 4);
            uint8_t count = G<uint8_t>(table + 1);
            uint8_t type = G<uint8_t>(weapon);
            if (entries && type < count) {
                uint32_t entry = entries + type * 0x20;
                uint64_t key = R32(entry) | (uint64_t(R32(entry + 4)) << 32);
                char s[13] = {};
                for (int i = 11; i >= 0; i--) {
                    int c = int(key % 40);
                    key /= 40;
                    s[i] = c == 0 ? ' ' : c == 1 ? '-' : c == 2 ? '/' : c < 13 ? char('0' + c - 3) : c < 39 ? char('A' + c - 13) : '_';
                }
                std::string name = s;
                if (name.find("SHG") != std::string::npos || name.find("RM1") != std::string::npos ||
                    name.find("SH5") != std::string::npos || name.find("RM5") != std::string::npos) {
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace

// The player's update, a frame (ecx the player, its class's own): god mode fills the health and holds the flag
// its damage calls test; noclip enables full 3D flight with WASD, Space (up) and Crouch (down)
static bool s_noclip_active = false;
static float s_noclip_pos[3] = {};
static uint32_t s_last_player = 0;

X86_EXTERN_FUNC(__imp__sub_001E5FA0);
X86_FUNC(sub_001E5FA0) {
    uint32_t me = ctx.ecx;
    bool is_player = me && me == Player() && R32(me + kCharKind) == kPlayerKind;
    bool is_noclip = is_player && game::Opt().noclip && !R32(me + kCharDying);

    if (is_player) {
        if ((game::Opt().god_mode || is_noclip) && !R32(me + kCharDying)) {
            float& health = G<float>(me + kCharHealth);
            health = std::max(health, FullHealth());
            G<uint8_t>(me + kCharInvulnerable) = 1;
            g_invulnerable = true;
        } else if (g_invulnerable && !game::Opt().god_mode && !is_noclip) {
            G<uint8_t>(me + kCharInvulnerable) = 0;
            g_invulnerable = false;
        }

        if (is_noclip) {
            if (!s_noclip_active || s_last_player != me) {
                s_noclip_pos[0] = G<float>(me + kCharPosition + 0);
                s_noclip_pos[1] = G<float>(me + kCharPosition + 4);
                s_noclip_pos[2] = G<float>(me + kCharPosition + 8);

                s_noclip_active = true;
                s_last_player = me;
            }

            HWND hwnd = static_cast<HWND>(app::Window());
            bool focused = hwnd && (GetForegroundWindow() == hwnd) && !app::MenuOpen();

            if (focused) {
                uint32_t world = R32(kWorld);
                float dt = world ? G<float>(world + kTimerDt) : 0.016666f;
                if (dt <= 0.0f || dt > 0.1f) dt = 0.016666f;

                // Camera Forward vector (fx, fy, fz)
                float fx = G<float>(me + kCharForward + 0);
                float fy = G<float>(me + kCharForward + 4);
                float fz = G<float>(me + kCharForward + 8);
                float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
                if (flen > 0.0001f) {
                    fx /= flen; fy /= flen; fz /= flen;
                }

                // Camera Right / Strafe vector (rx, ry, rz): cross product of forward with world UP (0, 1, 0)
                float rx = -fz;
                float ry = 0.0f;
                float rz = fx;
                float rlen = std::sqrt(rx * rx + rz * rz);
                if (rlen > 0.0001f) {
                    rx /= rlen; rz /= rlen;
                }

                // Keys: WASD, Space (up), Crouch (C or Left Ctrl: down), Shift (speed boost)
                bool key_w = (GetAsyncKeyState('W') & 0x8000) != 0 || (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
                bool key_s = (GetAsyncKeyState('S') & 0x8000) != 0 || (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
                bool key_a = (GetAsyncKeyState('A') & 0x8000) != 0 || (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
                bool key_d = (GetAsyncKeyState('D') & 0x8000) != 0 || (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
                bool key_space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
                bool key_crouch = (GetAsyncKeyState('C') & 0x8000) != 0 || (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                bool key_boost = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

                float move[3] = {0.0f, 0.0f, 0.0f};

                // Forward / Backward along 3D camera look direction
                if (key_w) { move[0] += fx; move[1] += fy; move[2] += fz; }
                if (key_s) { move[0] -= fx; move[1] -= fy; move[2] -= fz; }

                // Strafe Left / Right
                if (key_d) { move[0] += rx; move[1] += ry; move[2] += rz; }
                if (key_a) { move[0] -= rx; move[1] -= ry; move[2] -= rz; }

                // Vertical: Space for UP, Crouch for DOWN
                if (key_space) { move[1] += 1.0f; }
                if (key_crouch) { move[1] -= 1.0f; }

                float mlen = std::sqrt(move[0] * move[0] + move[1] * move[1] + move[2] * move[2]);
                if (mlen > 0.0001f) {
                    move[0] /= mlen;
                    move[1] /= mlen;
                    move[2] /= mlen;

                    float speed = key_boost ? 35.0f : 15.0f;
                    s_noclip_pos[0] += move[0] * speed * dt;
                    s_noclip_pos[1] += move[1] * speed * dt;
                    s_noclip_pos[2] += move[2] * speed * dt;
                }
            }

            // Sync coordinates before guest player update
            G<float>(me + kCharPosition + 0) = s_noclip_pos[0];
            G<float>(me + kCharPosition + 4) = s_noclip_pos[1];
            G<float>(me + kCharPosition + 8) = s_noclip_pos[2];

            G<float>(me + kCharPrevPos + 0) = s_noclip_pos[0];
            G<float>(me + kCharPrevPos + 4) = s_noclip_pos[1];
            G<float>(me + kCharPrevPos + 8) = s_noclip_pos[2];

            G<float>(me + kCharVelocity + 0) = 0.0f;
            G<float>(me + kCharVelocity + 4) = 0.0f;
            G<float>(me + kCharVelocity + 8) = 0.0f;
        } else {
            s_noclip_active = false;
        }
    }

    __imp__sub_001E5FA0(ctx, base);

    // Overwrite position after guest update so collisions and gravity never pull or displace the player
    if (is_noclip && me && me == Player() && !R32(me + kCharDying)) {
        G<float>(me + kCharPosition + 0) = s_noclip_pos[0];
        G<float>(me + kCharPosition + 4) = s_noclip_pos[1];
        G<float>(me + kCharPosition + 8) = s_noclip_pos[2];

        G<float>(me + kCharPrevPos + 0) = s_noclip_pos[0];
        G<float>(me + kCharPrevPos + 4) = s_noclip_pos[1];
        G<float>(me + kCharPrevPos + 8) = s_noclip_pos[2];

        G<float>(me + kCharVelocity + 0) = 0.0f;
        G<float>(me + kCharVelocity + 4) = 0.0f;
        G<float>(me + kCharVelocity + 8) = 0.0f;
    }
}

// A soldier takes a bullet (ecx the victim, the sixth of seven arguments its attacker, ret 0x1C): the
// characters' class, the player's has its own
X86_EXTERN_FUNC(__imp__sub_001E74B0);
X86_FUNC(sub_001E74B0) {
    Doomed doomed = Doom(ctx.ecx, R32(ctx.esp + 0x18));
    __imp__sub_001E74B0(ctx, base);
    Spare(doomed);
}

// A character's animation event (64-bit hash, then a pointer to the character): the throw takes a grenade
// (0x1E8934), the cheat puts the player's back
X86_EXTERN_FUNC(__imp__sub_001E8830);
X86_FUNC(sub_001E8830) {
    uint32_t who = R32(ctx.esp + 0xC);
    uint32_t me = who ? R32(who) : 0;
    bool keep = game::Opt().infinite_grenades && me && me == Player();
    uint16_t grenades = keep ? G<uint16_t>(me + kCharGrenades) : 0;
    __imp__sub_001E8830(ctx, base);
    if (keep && G<uint16_t>(me + kCharGrenades) < grenades) G<uint16_t>(me + kCharGrenades) = grenades;
}

// Bullet trajectory / spread calculation (esi shot, edx is_player, edi out normalized dir, [esp+4] &origin, ret 0x4)
// When no spread is active for the player, computes straight direction without any random deviation
X86_EXTERN_FUNC(__imp__sub_001CEF40);
X86_FUNC(sub_001CEF40) {
    uint32_t shot = ctx.esi;
    uint32_t weapon = shot ? R32(shot + 4) : 0;
    bool is_player = (ctx.edx & 0xFF) != 0 || (weapon && R32(weapon + 0xF0) == Player());
    if (game::Opt().no_spread && is_player && !IsShotgun(shot, weapon)) {
        uint32_t origin_ptr = R32(ctx.esp + 4);
        if (origin_ptr && weapon) {
            bool ads = G<uint8_t>(weapon + 0x3C) != 0;
            uint32_t target_ptr = weapon + (ads ? 0x20 : 0x10);

            float dx = G<float>(target_ptr + 0) - G<float>(origin_ptr + 0);
            float dy = G<float>(target_ptr + 4) - G<float>(origin_ptr + 4);
            float dz = G<float>(target_ptr + 8) - G<float>(origin_ptr + 8);
            float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (len > 0.00001f) {
                dx /= len;
                dy /= len;
                dz /= len;
                G<float>(ctx.edi + 0) = dx;
                G<float>(ctx.edi + 4) = dy;
                G<float>(ctx.edi + 8) = dz;
                G<float>(ctx.edi + 12) = 0.0f;
                ctx.esp += 8;
                return;
            }
        }
    }
    __imp__sub_001CEF40(ctx, base);
}

// Bullet spawning: proj_def is at [shot + 0x0C], and [proj_def + 0x14] is max range (float).
// When infinite bullet range is enabled for the player, extend max range during bullet creation
// so raycast and ballistic distance reach targets anywhere on the map.
X86_EXTERN_FUNC(__imp__sub_001DBD50);
X86_FUNC(sub_001DBD50) {
    uint32_t shot = R32(ctx.esp + 4);
    uint32_t weapon = shot ? R32(shot + 4) : 0;
    uint32_t owner = weapon ? R32(weapon + 0xF0) : 0;
    bool is_player = (owner && owner == Player()) || (owner && R32(owner + kCharKind) == kPlayerKind);
    uint32_t proj_def = (is_player && shot) ? R32(shot + 0x0C) : 0;
    float old_range = 0.0f;
    bool override_range = game::Opt().infinite_range && proj_def;
    if (override_range) {
        old_range = G<float>(proj_def + 0x14);
        G<float>(proj_def + 0x14) = 10000.0f;
    }
    __imp__sub_001DBD50(ctx, base);
    if (override_range) {
        G<float>(proj_def + 0x14) = old_range;
    }
}

// Projectile slot initialization:
// [esp + 4] = shot
// [esp + 8] = dir
// [esp + 12] = max_range
// [esp + 16] = speed
// [esp + 20] = flags
// When infinite range is enabled for player bullets, set range to 10000m and speed to 500,000 m/s
// so bullets travel instantaneously without bullet drop or travel delay.
X86_EXTERN_FUNC(__imp__sub_001DB7B0);
X86_FUNC(sub_001DB7B0) {
    uint32_t shot = R32(ctx.esp + 4);
    uint32_t weapon = shot ? R32(shot + 4) : 0;
    uint32_t owner = weapon ? R32(weapon + 0xF0) : 0;
    bool is_player = (owner && owner == Player()) || (owner && R32(owner + kCharKind) == kPlayerKind);
    if (game::Opt().infinite_range && is_player) {
        G<float>(ctx.esp + 12) = 10000.0f;
        G<float>(ctx.esp + 16) = 500000.0f;
    }
    __imp__sub_001DB7B0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_001DB730);
X86_FUNC(sub_001DB730) {
    uint32_t shot = R32(ctx.esp + 4);
    uint32_t weapon = shot ? R32(shot + 4) : 0;
    uint32_t owner = weapon ? R32(weapon + 0xF0) : 0;
    bool is_player = (owner && owner == Player()) || (owner && R32(owner + kCharKind) == kPlayerKind);
    if (game::Opt().infinite_range && is_player) {
        G<float>(ctx.esp + 12) = 10000.0f;
        G<float>(ctx.esp + 16) = 500000.0f;
    }
    __imp__sub_001DB730(ctx, base);
}





