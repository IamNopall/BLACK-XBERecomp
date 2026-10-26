// The cheats page's god mode, one-hit kill and infinite grenades. The rest live where the game
// already has a hook: infinite ammo, rapid fire and no recoil with the weapon's, game speed with the frame time
#include <algorithm>
#include <cmath>
#include <string>

#include "common/options.h"
#include "game/layout.h"
#include "xdk/hle.h"

using namespace kernel;
using namespace layout;

namespace {

// character: the player is the one in the world container, its kind 2
constexpr uint32_t kCharKind = 0xC4;
constexpr uint32_t kCharGrenades = 0x286;  // uint16
constexpr uint32_t kCharHealth = 0x2F0;    // float, the difficulty's full at most
constexpr uint32_t kCharDying = 0x384;     // not 0 once it is killed
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
// its damage calls test
X86_EXTERN_FUNC(__imp__sub_001E5FA0);
X86_FUNC(sub_001E5FA0) {
    uint32_t me = ctx.ecx;
    if (me && me == Player() && R32(me + kCharKind) == kPlayerKind) {
        if (game::Opt().god_mode && !R32(me + kCharDying)) {
            float& health = G<float>(me + kCharHealth);
            health = std::max(health, FullHealth());
            G<uint8_t>(me + kCharInvulnerable) = 1;
            g_invulnerable = true;
        } else if (g_invulnerable) {
            G<uint8_t>(me + kCharInvulnerable) = 0;
            g_invulnerable = false;
        }
    }
    __imp__sub_001E5FA0(ctx, base);
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





