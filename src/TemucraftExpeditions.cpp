/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

/*
 * mod-temucraft-expeditions: Temucraft expeditions on top of any dungeon.
 *
 * Talk to an Expedition Master (creature 608805), pick a dungeon and a tier, and you (and up to four party members near you)
 * are sent in. Inside, the party is moved to the expedition phase (default 2): the dungeon's own creatures, doors and
 * bosses are in phase 1 and stay out of the way, and the expedition spawns its own enemies in phase 2, scaled to your level,
 * the tier, the party size and this week's affixes. Kill the expedition boss and an Expedition Cache appears with the rewards.
 *
 * - Every 5-player dungeon is an expedition when Temucraft.Expeditions.AllDungeons = 1: enemies are placed on the dungeon's own
 *   creature spots (unscripted, hostile ones), spread from the entrance to the far end; the expedition boss stands on the
 *   farthest elite's spot. Which spots are used changes every day (Temucraft.Expeditions.Variants story variants).
 *   `mod_temucraft_expedition` adds or overrides expeditions (name, level, entrance, enemy count, boss, category, on/off);
 *   `mod_temucraft_expedition_spawn` replaces the automatic layout with a hand-made one per variant.
 * - Tiers (`mod_temucraft_expedition_tier`): health and damage multipliers, lives (0 = unlimited), rewards. Each death past
 *   the lives fails the run; each death lowers the cache's rewards. Tiers unlock one at a time per character.
 * - Rich expeditions: a few per day (same for everyone). The first rich cache each day per character gives the
 *   rich reward too (optionally needing a key item).
 * - Affixes (from tier Temucraft.Expeditions.Affix.FromTier, a second one from Affix.SecondFromTier), changing weekly:
 *   Hardened (enemies +25% health), Warlord's (boss +30% health, +15% damage), Brutal (+15% damage),
 *   Swarming (+30% enemies).
 * - Companions (mod-temucraft-companions) can come along when their activity mask has the expedition bit (4).
 *
 * Commands: .expedition leave | status ; GM: .expedition start <map> <tier>, .expedition list ; admin: .expedition reload
 * Text comes from module_string (module "mod-temucraft-expeditions"), with English built in as a fallback.
 */
#include "Chat.h"
#include "ChatCommand.h"
#include "CommandScript.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureScript.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "GameObjectScript.h"
#include "GameTime.h"
#include "Group.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "SpellInfo.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "TemucraftModuleText.h"
#include "UnitScript.h"
#include "World.h"
#include "WorldScript.h"
#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <unordered_map>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    constexpr char const* MODULE = "mod-temucraft-expeditions";
    constexpr uint32 NPC_KEEPER = 608805, GO_CACHE = 608806;
    constexpr uint32 TEXT_KEEPER = 608805, TEXT_CACHE = 608806;

    enum Str : uint32
    {
        S_OFF = 1, S_RICH_HEAD, S_CATEGORY, S_AFFIXES, S_NO_AFFIXES, S_BACK, S_NEXT, S_EXPEDITION_LINE, S_TIER_LINE,
        S_TIER_LOCKED, S_LEVEL, S_LEADER, S_TOO_MANY, S_RAID, S_ENTER, S_BOSS_DOWN, S_DEATH, S_FAILED, S_OPEN, S_LEAVE,
        S_NOT_DONE, S_CLAIMED, S_REWARD_GOLD, S_REWARD_ITEM, S_RICH_REWARD, S_NEED_KEY, S_NOT_IN_EXPEDITION, S_STATUS,
        S_NO_EXPEDITION, S_RICH_MARK, S_ENEMIES_LEFT, S_NOTHING_TODAY, S_TIER_UNLOCKED, S_BUSY_ELSEWHERE, S_SHARDS, S_KEY_MADE, S_NEED_VKEY, S_WALLET
    };

    char const* const CATEGORY_NAMES[] = { "Classic dungeons", "Burning Crusade dungeons", "Wrath of the Lich King dungeons",
                                           "Cataclysm dungeons", "Mists of Pandaria dungeons", "Scenarios", "Temucraft and other dungeons" };
    constexpr uint8 MAX_CATEGORY = 7;
    enum Affix : uint8 { AF_FORTIFIED = 1, AF_TYRANNICAL = 2, AF_BRUTAL = 4, AF_SWARMING = 8 };
    char const* const AFFIX_NAMES[] = { "Hardened", "Warlord's", "Brutal", "Swarming" };
    enum Kind : uint8 { K_MOB = 0, K_ELITE = 1, K_BOSS = 2, K_CACHE = 3 };

    struct Settings
    {
        bool enable = true, allDungeons = true, unlockAll = false;
        uint32 phase = 2, minLevel = 10, maxPlayers = 5, mobCount = 16, minPoints = 8, variants = 3, rich = 3, keyItem = 0;
        float groupRange = 40.0f;
        float healthMob = 1.0f, healthElite = 2.5f, healthBoss = 8.0f, damageMob = 1.0f, damageElite = 1.5f, damageBoss = 2.0f;
        float partyHealth = 0.5f, partyDamage = 0.1f;
        uint32 deathPenalty = 20, affixFrom = 4, affixSecond = 8;
        float richGold = 1.0f;
        uint32 richItem = 0, richCount = 0;
        bool keys = true;
        uint32 shardsPerKey = 100, shardsPerTier = 10;
    } cfg;

    struct Tier { float health = 1, damage = 1; uint32 lives = 0, gold = 0, item = 0, itemCount = 0, bonusItem = 0, bonusCount = 0; };
    struct Point { uint32 entry = 0; uint8 rank = 0; Position pos; };
    struct HandSpawn { uint8 kind = K_MOB; uint32 entry = 0; Position pos; };
    struct Expedition
    {
        uint32 map = 0;
        std::string name;
        bool enabled = true;
        uint8 minLevel = 0, category = 255;
        Position entrance;
        bool hasEntrance = false;
        uint32 mobCount = 0, bossEntry = 0;
        std::vector<Point> points;                        // sorted by distance from the entrance
        std::map<uint8, std::vector<HandSpawn>> hand;     // variant -> layout (variant 0 = every day)
    };
    struct Run
    {
        uint32 map = 0, instance = 0, token = 0, tier = 1, lives = 0, deaths = 0, kills = 0, total = 0, started = 0;
        uint8 affixes = 0;
        bool done = false, failed = false, rich = false;
        ObjectGuid boss, cache;
        std::vector<ObjectGuid> creatures;
        std::set<ObjectGuid::LowType> members, claimed;
    };
    struct Pending { uint32 map = 0, tier = 1, token = 0, at = 0; WorldLocation from; };
    struct Party { uint32 count = 1; uint8 level = 1; };

    std::map<uint32, Expedition> expeditions;                                // by map id
    std::map<uint32, Tier> tiers;                                  // by tier
    std::unordered_map<uint32, Run> runs;                          // by instance id
    std::unordered_map<ObjectGuid::LowType, Pending> pending;      // by player
    std::unordered_map<ObjectGuid::LowType, WorldLocation> returnTo;
    std::unordered_map<ObjectGuid::LowType, uint32> previousPhase;
    std::unordered_map<ObjectGuid::LowType, uint32> viewing;       // expedition chosen at the keeper
    std::unordered_map<uint32, Party> parties;                     // by token
    std::unordered_map<ObjectGuid, std::pair<uint32, float>> enemies; // creature -> (instance, spell damage factor)
    uint32 nextToken = 1;

    template<typename T>
    char const* Text(T who, uint32 id, char const* fallback) { return TemucraftModuleText(MODULE, who, id, fallback); }

    uint32 Now() { return uint32(GameTime::GetGameTime().count()); }
    uint32 Day() { return Now() / 86400; }
    uint32 Week() { return (Now() + 3 * 86400) / 604800; }

    // ------------------------------------------------------------------------------------------- settings and data
    void LoadSettings()
    {
        cfg.enable = sConfigMgr->GetOption<bool>("Temucraft.Expeditions.Enable", true);
        cfg.allDungeons = sConfigMgr->GetOption<bool>("Temucraft.Expeditions.AllDungeons", true);
        cfg.unlockAll = sConfigMgr->GetOption<bool>("Temucraft.Expeditions.UnlockAllTiers", false);
        cfg.phase = std::max<uint32>(2, sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Phase", 2));
        cfg.minLevel = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.MinLevel", 10);
        cfg.maxPlayers = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.MaxPlayers", 5), 1, 5);
        cfg.groupRange = sConfigMgr->GetOption<float>("Temucraft.Expeditions.GroupRange", 40.0f);
        cfg.mobCount = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Enemies", 16));
        cfg.minPoints = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.MinSpots", 8);
        cfg.variants = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Variants", 3));
        cfg.healthMob = sConfigMgr->GetOption<float>("Temucraft.Expeditions.Health.Normal", 1.0f);
        cfg.healthElite = sConfigMgr->GetOption<float>("Temucraft.Expeditions.Health.Elite", 2.5f);
        cfg.healthBoss = sConfigMgr->GetOption<float>("Temucraft.Expeditions.Health.Boss", 8.0f);
        cfg.damageMob = sConfigMgr->GetOption<float>("Temucraft.Expeditions.Damage.Normal", 1.0f);
        cfg.damageElite = sConfigMgr->GetOption<float>("Temucraft.Expeditions.Damage.Elite", 1.5f);
        cfg.damageBoss = sConfigMgr->GetOption<float>("Temucraft.Expeditions.Damage.Boss", 2.0f);
        cfg.partyHealth = sConfigMgr->GetOption<float>("Temucraft.Expeditions.PerExtraPlayer.Health", 0.5f);
        cfg.partyDamage = sConfigMgr->GetOption<float>("Temucraft.Expeditions.PerExtraPlayer.Damage", 0.1f);
        cfg.deathPenalty = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.DeathPenalty", 20);
        cfg.affixFrom = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Affix.FromTier", 4);
        cfg.affixSecond = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Affix.SecondFromTier", 8);
        cfg.rich = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Rich.PerDay", 3);
        cfg.keyItem = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Rich.KeyItem", 0);
        cfg.richGold = sConfigMgr->GetOption<float>("Temucraft.Expeditions.Rich.GoldMultiplier", 1.0f);
        cfg.richItem = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Rich.Item", 0);
        cfg.richCount = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Rich.ItemCount", 1);
        cfg.keys = sConfigMgr->GetOption<bool>("Temucraft.Expeditions.Rich.Keys", true);
        cfg.shardsPerKey = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Rich.ShardsPerKey", 100));
        cfg.shardsPerTier = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Rich.ShardsPerTier", 10);
    }

    uint8 AutoCategory(uint32 map)
    {
        static std::set<uint32> const tbc = { 269, 540, 542, 543, 545, 546, 547, 552, 553, 554, 555, 556, 557, 558, 560, 585 };
        static std::set<uint32> const wrath = { 574, 575, 576, 578, 595, 599, 600, 601, 602, 604, 608, 619, 632, 650, 658, 668 };
        static std::set<uint32> const cata = { 568, 643, 644, 645, 657, 670, 725, 755, 859, 938, 939, 940 };
        static std::set<uint32> const mists = { 959, 960, 961, 962, 994, 1001, 1004, 1007, 1011 };
        if (tbc.count(map)) return 1;
        if (wrath.count(map)) return 2;
        if (cata.count(map)) return 3;
        if (mists.count(map)) return 4;
        if (map < 530) return 0;
        if (map >= 1000 && map < 1200) return 5;
        return 6;
    }

    void LoadData()
    {
        expeditions.clear(); tiers.clear();
        LocaleConstant locale = sWorld->GetDefaultDbcLocale();

        if (QueryResult r = WorldDatabase.Query("SELECT tier, health, damage, lives, gold, item, item_count, bonus_item, bonus_count FROM mod_temucraft_expedition_tier"))
            do
            {
                Field* f = r->Fetch(); Tier t;
                t.health = f[1].Get<float>(); t.damage = f[2].Get<float>(); t.lives = f[3].Get<uint32>(); t.gold = f[4].Get<uint32>();
                t.item = f[5].Get<uint32>(); t.itemCount = f[6].Get<uint32>(); t.bonusItem = f[7].Get<uint32>(); t.bonusCount = f[8].Get<uint32>();
                tiers[f[0].Get<uint32>()] = t;
            } while (r->NextRow());
        if (tiers.empty()) tiers[1] = Tier();

        if (cfg.allDungeons)
            for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
                if (MapEntry const* e = sMapStore.LookupEntry(i))
                    if (e->IsNonRaidDungeon())
                    {
                        Expedition& d = expeditions[e->MapID];
                        d.map = e->MapID;
                        d.name = e->name[locale] && *e->name[locale] ? e->name[locale] : (e->name[0] && *e->name[0] ? e->name[0] : Acore::StringFormat("Map {}", e->MapID));
                    }

        if (QueryResult r = WorldDatabase.Query("SELECT map, name, enabled, min_level, entrance_x, entrance_y, entrance_z, entrance_o, enemies, boss_entry, category FROM mod_temucraft_expedition"))
            do
            {
                Field* f = r->Fetch();
                uint32 map = f[0].Get<uint32>();
                MapEntry const* e = sMapStore.LookupEntry(map);
                if (!e || !e->IsDungeon()) { LOG_ERROR("module", "mod-temucraft-expeditions: map {} is not a dungeon, skipped", map); continue; }
                Expedition& d = expeditions[map];
                d.map = map;
                if (!f[1].Get<std::string>().empty()) d.name = f[1].Get<std::string>();
                else if (d.name.empty()) d.name = e->name[locale] ? e->name[locale] : Acore::StringFormat("Map {}", map);
                d.enabled = f[2].Get<bool>(); d.minLevel = f[3].Get<uint8>();
                float x = f[4].Get<float>(), y = f[5].Get<float>(), z = f[6].Get<float>();
                if (x != 0.0f || y != 0.0f || z != 0.0f) { d.entrance.Relocate(x, y, z, f[7].Get<float>()); d.hasEntrance = true; }
                d.mobCount = f[8].Get<uint32>(); d.bossEntry = f[9].Get<uint32>(); d.category = f[10].Get<uint8>();
            } while (r->NextRow());

        // entrances from the dungeons' own entrance area triggers (prefer one named "entrance")
        if (QueryResult r = WorldDatabase.Query("SELECT target_map, target_position_x, target_position_y, target_position_z, target_orientation, "
            "(LOWER(Name) LIKE '%entrance%') AS e, (LOWER(Name) LIKE '%exit%') AS x FROM areatrigger_teleport ORDER BY e DESC, x ASC, ID ASC"))
            do
            {
                Field* f = r->Fetch();
                auto it = expeditions.find(f[0].Get<uint32>());
                if (it == expeditions.end() || it->second.hasEntrance) continue;
                it->second.entrance.Relocate(f[1].Get<float>(), f[2].Get<float>(), f[3].Get<float>(), f[4].Get<float>());
                it->second.hasEntrance = true;
            } while (r->NextRow());

        if (!expeditions.empty())
        {
            std::string maps;
            for (auto const& [id, d] : expeditions) { if (!maps.empty()) maps += ','; maps += std::to_string(id); }
            // the dungeon's own spots: normal-difficulty, unscripted, attackable, hostile-looking, no vendors, critters or bosses
            if (QueryResult r = WorldDatabase.Query("SELECT c.map, c.id, c.position_x, c.position_y, c.position_z, c.orientation, ct.`rank` FROM creature c "
                "JOIN creature_template ct ON ct.entry = c.id WHERE c.map IN ({}) AND (c.spawnMask & 1) <> 0 AND ct.ScriptName = '' AND ct.npcflag = 0 "
                "AND (ct.unit_flags & 33554690) = 0 AND ct.type NOT IN (8, 11) AND ct.`rank` <> 3 AND ct.faction NOT IN (7, 31, 35) ORDER BY c.guid", maps))
                do
                {
                    Field* f = r->Fetch(); Point p;
                    p.entry = f[1].Get<uint32>(); p.rank = f[6].Get<uint8>();
                    p.pos.Relocate(f[2].Get<float>(), f[3].Get<float>(), f[4].Get<float>(), f[5].Get<float>());
                    expeditions[f[0].Get<uint32>()].points.push_back(p);
                } while (r->NextRow());
        }

        if (QueryResult r = WorldDatabase.Query("SELECT map, variant, kind, entry, x, y, z, o FROM mod_temucraft_expedition_spawn"))
            do
            {
                Field* f = r->Fetch();
                auto it = expeditions.find(f[0].Get<uint32>());
                if (it == expeditions.end()) continue;
                HandSpawn h; h.kind = f[2].Get<uint8>(); h.entry = f[3].Get<uint32>();
                h.pos.Relocate(f[4].Get<float>(), f[5].Get<float>(), f[6].Get<float>(), f[7].Get<float>());
                it->second.hand[f[1].Get<uint8>()].push_back(h);
            } while (r->NextRow());

        for (auto it = expeditions.begin(); it != expeditions.end();)
        {
            Expedition& d = it->second;
            if (d.category >= MAX_CATEGORY) d.category = AutoCategory(d.map);
            bool usable = d.hasEntrance && (d.points.size() >= std::max<uint32>(2, cfg.minPoints) || !d.hand.empty());
            if (!usable) { it = expeditions.erase(it); continue; }
            Position e = d.entrance;
            std::stable_sort(d.points.begin(), d.points.end(), [&e](Point const& a, Point const& b) { return e.GetExactDist(&a.pos) < e.GetExactDist(&b.pos); });
            ++it;
        }
        LOG_INFO("module", "mod-temucraft-expeditions: {} expeditions, {} tiers", expeditions.size(), tiers.size());
    }

    uint8 WeeklyAffixes(uint32 tier)
    {
        if (tier < cfg.affixFrom) return 0;
        uint32 w = Week();
        uint8 first = uint8(w % 4), second = uint8((first + 1 + (w / 4) % 3) % 4);
        uint8 mask = uint8(1 << first);
        if (tier >= cfg.affixSecond) mask |= uint8(1 << second);
        return mask;
    }
    std::string AffixNames(uint8 mask)
    {
        std::string out;
        for (uint8 i = 0; i < 4; ++i) if (mask & (1 << i)) { if (!out.empty()) out += ", "; out += AFFIX_NAMES[i]; }
        return out;
    }

    std::vector<uint32> RichToday()
    {
        std::vector<uint32> maps;
        for (auto const& [id, d] : expeditions) if (d.enabled) maps.push_back(id);
        std::mt19937 day(Day());
        std::shuffle(maps.begin(), maps.end(), day);
        if (maps.size() > cfg.rich) maps.resize(cfg.rich);
        return maps;
    }
    bool IsRich(uint32 map)
    {
        std::vector<uint32> b = RichToday();
        return std::find(b.begin(), b.end(), map) != b.end();
    }

    Tier const& TierData(uint32 tier)
    {
        auto it = tiers.find(tier);
        return it != tiers.end() ? it->second : tiers.begin()->second;
    }
    uint32 MaxTier() { return tiers.rbegin()->first; }

    uint32 BestTier(Player* player, uint32 map)
    {
        if (QueryResult r = CharacterDatabase.Query("SELECT best_tier FROM mod_temucraft_expedition_progress WHERE owner = {} AND map = {}", player->GetGUID().GetCounter(), map))
            return r->Fetch()[0].Get<uint32>();
        return 0;
    }

    Run* RunFor(Player* player)
    {
        Map* map = player->GetMap();
        if (!map || !map->IsDungeon()) return nullptr;
        auto it = runs.find(map->GetInstanceId());
        if (it == runs.end() || it->second.map != map->GetId() || !it->second.members.count(player->GetGUID().GetCounter())) return nullptr;
        return &it->second;
    }

    // ------------------------------------------------------------------------------------------- phases
    void EnterPhase(Player* player)
    {
        if (player->GetPhaseMask() == cfg.phase) return;
        previousPhase[player->GetGUID().GetCounter()] = player->GetPhaseMask();
        player->SetPhaseMask(cfg.phase, true);
    }
    // only undoes what EnterPhase did (other content, e.g. the death knight start, uses phase 2 too)
    void LeavePhase(Player* player)
    {
        auto it = previousPhase.find(player->GetGUID().GetCounter());
        if (it == previousPhase.end()) return;
        if (player->GetPhaseMask() != cfg.phase) { previousPhase.erase(it); return; }
        uint32 phase = it->second != cfg.phase ? it->second : PHASEMASK_NORMAL;
        previousPhase.erase(it);
        player->SetPhaseMask(phase, true);
    }

    // ------------------------------------------------------------------------------------------- spawning
    void Scale(Creature* c, Kind kind, Party const& party, Run const& run)
    {
        CreatureTemplate const* t = c->GetCreatureTemplate();
        CreatureBaseStats const* orig = sObjectMgr->GetCreatureBaseStats(c->GetLevel(), t->unit_class);
        CreatureBaseStats const* s = sObjectMgr->GetCreatureBaseStats(party.level, t->unit_class);
        Tier const& tier = TierData(run.tier);
        float health = (kind == K_BOSS ? cfg.healthBoss : kind == K_ELITE ? cfg.healthElite : cfg.healthMob) * tier.health * (1.0f + cfg.partyHealth * (party.count - 1));
        float damage = tier.damage * (1.0f + cfg.partyDamage * (party.count - 1));
        if ((run.affixes & AF_FORTIFIED) && kind != K_BOSS) health *= 1.25f;
        if ((run.affixes & AF_TYRANNICAL) && kind == K_BOSS) { health *= 1.3f; damage *= 1.15f; }
        if (run.affixes & AF_BRUTAL) damage *= 1.15f;
        float weapon = damage * (kind == K_BOSS ? cfg.damageBoss : kind == K_ELITE ? cfg.damageElite : cfg.damageMob);

        c->SetLevel(party.level);
        uint32 hp = std::max<uint32>(1, uint32(s->BaseHealth[EXPANSION_WRATH_OF_THE_LICH_KING] * health));
        c->SetCreateHealth(hp);
        c->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(hp));
        float base = s->BaseDamage[EXPANSION_WRATH_OF_THE_LICH_KING] * weapon;
        for (WeaponAttackType a : { BASE_ATTACK, OFF_ATTACK, RANGED_ATTACK })
        {
            c->SetBaseWeaponDamage(a, MINDAMAGE, base);
            c->SetBaseWeaponDamage(a, MAXDAMAGE, base * 1.5f);
        }
        c->SetStatFlatModifier(UNIT_MOD_ATTACK_POWER, BASE_VALUE, float(s->AttackPower));
        c->SetStatFlatModifier(UNIT_MOD_ATTACK_POWER_RANGED, BASE_VALUE, float(s->RangedAttackPower));
        c->SetStatFlatModifier(UNIT_MOD_ARMOR, BASE_VALUE, s->GenerateArmor(t));
        c->UpdateAllStats();
        c->SetHealth(c->GetMaxHealth());

        // spells keep their own numbers, so they are scaled by how much stronger the level made the creature
        float before = orig ? orig->BaseDamage[std::min<uint32>(t->expansion, EXPANSION_WRATH_OF_THE_LICH_KING)] : 0.0f;
        float spell = before > 0.0f ? s->BaseDamage[EXPANSION_WRATH_OF_THE_LICH_KING] / before : 1.0f;
        enemies[c->GetGUID()] = { run.instance, std::clamp(spell, 0.5f, 50.0f) * damage * (kind == K_BOSS ? 1.25f : 1.0f) };
    }

    Creature* Place(Map* map, Run& run, uint32 entry, Position const& pos, Kind kind, Party const& party)
    {
        if (!entry || !sObjectMgr->GetCreatureTemplate(entry)) return nullptr;
        map->LoadGrid(pos.GetPositionX(), pos.GetPositionY());
        TempSummon* c = map->SummonCreature(entry, pos);
        if (!c) return nullptr;
        c->SetPhaseMask(cfg.phase, true);
        Scale(c, kind, party, run);
        run.creatures.push_back(c->GetGUID());
        return c;
    }

    void Despawn(Map* map, Run& run)
    {
        for (ObjectGuid const& g : run.creatures)
        {
            enemies.erase(g);
            if (map)
                if (Creature* c = map->GetCreature(g)) c->DespawnOrUnsummon();
        }
        run.creatures.clear();
        if (map && run.cache)
            if (GameObject* go = map->GetGameObject(run.cache)) go->Delete();
        run.cache.Clear();
    }

    void Populate(Map* map, Run& run, Party const& party)
    {
        Expedition const& d = expeditions.at(run.map);
        uint32 variant = (Day() + d.map) % cfg.variants + 1;
        auto hand = d.hand.find(uint8(variant));
        if (hand == d.hand.end()) hand = d.hand.find(0);
        if (hand != d.hand.end())
        {
            for (HandSpawn const& h : hand->second)
                if (h.kind != K_CACHE)
                    if (Creature* c = Place(map, run, h.entry, h.pos, Kind(h.kind), party))
                        if (h.kind == K_BOSS) run.boss = c->GetGUID();
        }
        else
        {
            std::vector<Point> const& pts = d.points;
            // boss: the farthest elite spot (or the farthest spot)
            size_t bossIdx = pts.size() - 1;
            for (size_t i = pts.size(); i-- > 0;) if (pts[i].rank >= 1) { bossIdx = i; break; }
            uint32 n = d.mobCount ? d.mobCount : cfg.mobCount;
            if (run.affixes & AF_SWARMING) n = n * 13 / 10;
            n = std::min<uint32>(n, uint32(pts.size() - 1));
            float step = float(pts.size() - 1) / float(std::max<uint32>(1, n));
            float offset = step * float(variant - 1) / float(cfg.variants);
            std::set<size_t> used;
            for (uint32 i = 0; i < n; ++i)
            {
                size_t idx = std::min<size_t>(size_t(offset + step * i), pts.size() - 1);
                if (idx == bossIdx) idx = idx ? idx - 1 : idx + 1;
                if (!used.insert(idx).second || idx >= pts.size()) continue;
                Place(map, run, pts[idx].entry, pts[idx].pos, pts[idx].rank >= 1 ? K_ELITE : K_MOB, party);
            }
            uint32 bossEntry = d.bossEntry ? d.bossEntry : pts[bossIdx].entry;
            if (Creature* c = Place(map, run, bossEntry, pts[bossIdx].pos, K_BOSS, party)) run.boss = c->GetGUID();
        }
        run.total = uint32(run.creatures.size());
    }

    void StartRun(Player* player, Pending const& p)
    {
        Map* map = player->GetMap();
        uint32 instance = map->GetInstanceId();
        Run& run = runs[instance];
        if (run.token) Despawn(map, run);
        run = Run();
        run.map = p.map; run.instance = instance; run.token = p.token; run.tier = p.tier; run.started = Now();
        run.lives = TierData(p.tier).lives;
        run.affixes = WeeklyAffixes(p.tier);
        run.rich = IsRich(p.map);
        Party party = parties.count(p.token) ? parties[p.token] : Party{ 1, player->GetLevel() };
        Populate(map, run, party);
    }

    // a player arrived on a map (teleport, login, corpse run)
    void Arrive(Player* player)
    {
        Map* map = player->GetMap();
        ObjectGuid::LowType me = player->GetGUID().GetCounter();
        if (!map) return;
        auto p = pending.find(me);
        if (p != pending.end() && map->IsDungeon() && map->GetId() == p->second.map && expeditions.count(p->second.map))
        {
            Pending pend = p->second;
            pending.erase(p);
            auto r = runs.find(map->GetInstanceId());
            if (r == runs.end() || r->second.token != pend.token) StartRun(player, pend);
            Run& run = runs[map->GetInstanceId()];
            run.members.insert(me);
            EnterPhase(player);
            {
                Expedition const& d = expeditions.at(run.map);
                ChatHandler(player->GetSession()).PSendSysMessage(Text(player, S_ENTER, "|cffffcc00Expedition:|r {} - tier {}{}. Defeat the expedition boss deep inside; {} enemies await."),
                    d.name, run.tier, run.affixes ? Acore::StringFormat(" ({})", AffixNames(run.affixes)) : std::string(), run.total);
            }
            return;
        }
        if (RunFor(player)) { EnterPhase(player); return; }
        LeavePhase(player);
    }

    // ------------------------------------------------------------------------------------------- entering
    bool Enter(Player* player, uint32 mapId, uint32 tier, bool gm)
    {
        ChatHandler h(player->GetSession());
        auto it = expeditions.find(mapId);
        if (it == expeditions.end()) { h.SendSysMessage(Text(player, S_NO_EXPEDITION, "There is no such expedition.")); return false; }
        Expedition const& d = it->second;
        uint32 minLevel = std::max<uint32>(cfg.minLevel, d.minLevel);
        std::vector<Player*> party;
        if (Group* g = player->GetGroup())
        {
            if (g->isRaidGroup()) { h.SendSysMessage(Text(player, S_RAID, "Expeditions are for parties, not raids.")); return false; }
            if (!g->IsLeader(player->GetGUID())) { h.SendSysMessage(Text(player, S_LEADER, "Only the party leader can choose the expedition.")); return false; }
            for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
                if (Player* m = ref->GetSource())
                    if (m == player || (m->IsInWorld() && m->GetMap() == player->GetMap() && m->IsWithinDistInMap(player, cfg.groupRange) && m->IsAlive()))
                        party.push_back(m);
        }
        else
            party.push_back(player);
        if (party.size() > cfg.maxPlayers) { h.PSendSysMessage(Text(player, S_TOO_MANY, "Expeditions are for up to {} players."), cfg.maxPlayers); return false; }
        Party info; info.count = uint32(party.size());
        for (Player* m : party)
        {
            if (!gm && m->GetLevel() < minLevel)
            {
                h.PSendSysMessage(Text(player, S_LEVEL, "Everyone needs to be level {} for this expedition."), minLevel);
                return false;
            }
            if (RunFor(m) || (m->GetMap() && m->GetMap()->IsDungeon()))
            {
                h.SendSysMessage(Text(player, S_BUSY_ELSEWHERE, "Someone in your party is inside a dungeon. Leave it first."));
                return false;
            }
            info.level = std::max<uint8>(info.level, m->GetLevel());
        }
        // normal difficulty: no heroic lockouts are spent
        if (Group* g = player->GetGroup()) { if (g->GetDungeonDifficulty() != DUNGEON_DIFFICULTY_NORMAL) g->SetDungeonDifficulty(DUNGEON_DIFFICULTY_NORMAL); }
        else if (player->GetDungeonDifficulty() != DUNGEON_DIFFICULTY_NORMAL) { player->SetDungeonDifficulty(DUNGEON_DIFFICULTY_NORMAL); player->SendDungeonDifficulty(false); }

        uint32 token = nextToken++;
        parties[token] = info;
        for (Player* m : party)
        {
            Pending p; p.map = mapId; p.tier = tier; p.token = token; p.at = Now();
            p.from.WorldRelocate(m->GetMapId(), m->GetPositionX(), m->GetPositionY(), m->GetPositionZ(), m->GetOrientation());
            pending[m->GetGUID().GetCounter()] = p;
            returnTo[m->GetGUID().GetCounter()] = p.from;
            m->TeleportTo(mapId, d.entrance.GetPositionX(), d.entrance.GetPositionY(), d.entrance.GetPositionZ(), d.entrance.GetOrientation());
        }
        return true;
    }

    void Leave(Player* player)
    {
        ObjectGuid::LowType me = player->GetGUID().GetCounter();
        if (Run* run = RunFor(player)) run->members.erase(me);
        LeavePhase(player);
        auto it = returnTo.find(me);
        if (it != returnTo.end()) { WorldLocation loc = it->second; returnTo.erase(it); player->TeleportTo(loc); }
        else player->TeleportTo(player->m_homebindMapId, player->m_homebindX, player->m_homebindY, player->m_homebindZ, player->GetOrientation());
    }

    // ------------------------------------------------------------------------------------------- key fragments
    // Cache Key Fragments come from every cache (ShardsPerTier x tier); every ShardsPerKey of them become a Restored
    // Cache Key, which opens the rich part of a rich cache. Kept per character, no item needed.
    std::pair<uint32, uint32> Wallet(ObjectGuid::LowType owner)
    {
        if (QueryResult r = CharacterDatabase.Query("SELECT shards, `keys` FROM mod_temucraft_expedition_wallet WHERE owner = {}", owner))
            return { r->Fetch()[0].Get<uint32>(), r->Fetch()[1].Get<uint32>() };
        return { 0, 0 };
    }
    void SaveWallet(ObjectGuid::LowType owner, uint32 shards, uint32 keys)
    {
        CharacterDatabase.DirectExecute("REPLACE INTO mod_temucraft_expedition_wallet (owner, shards, `keys`) VALUES ({}, {}, {})", owner, shards, keys);
    }

    // ------------------------------------------------------------------------------------------- rewards
    void Reward(Player* player, Run& run)
    {
        ChatHandler h(player->GetSession());
        ObjectGuid::LowType me = player->GetGUID().GetCounter();
        if (!run.done) { h.SendSysMessage(Text(player, S_NOT_DONE, "The cache stays shut until the expedition boss is defeated.")); return; }
        if (run.claimed.count(me)) { h.SendSysMessage(Text(player, S_CLAIMED, "You already opened your cache.")); return; }
        run.claimed.insert(me);
        Tier const& t = TierData(run.tier);
        uint32 keep = run.deaths * cfg.deathPenalty >= 100 ? 0 : 100 - run.deaths * cfg.deathPenalty;
        uint32 gold = t.gold * keep / 100;
        if (gold) { player->ModifyMoney(gold); h.PSendSysMessage(Text(player, S_REWARD_GOLD, "  Cache: {} gold."), gold / 10000); }
        auto give = [&](uint32 item, uint32 count)
        {
            if (!item || !count || !sObjectMgr->GetItemTemplate(item)) return;
            if (player->AddItem(item, count))
                h.PSendSysMessage(Text(player, S_REWARD_ITEM, "  Cache: {} x{}."), sObjectMgr->GetItemTemplate(item)->Name1, count);
        };
        if (keep >= 50) give(t.item, t.itemCount);
        if (run.deaths == 0) give(t.bonusItem, t.bonusCount);
        uint32 day = Day();
        if (run.rich)
        {
            uint32 last = 0;
            if (QueryResult r = CharacterDatabase.Query("SELECT rich_day FROM mod_temucraft_expedition_progress WHERE owner = {} AND map = {}", me, run.map))
                last = r->Fetch()[0].Get<uint32>();
            QueryResult any = CharacterDatabase.Query("SELECT 1 FROM mod_temucraft_expedition_progress WHERE owner = {} AND rich_day = {} LIMIT 1", me, day);
            if (last != day && !any)
            {
                auto wallet = Wallet(me);
                if (cfg.keyItem && !player->HasItemCount(cfg.keyItem, 1))
                    h.PSendSysMessage(Text(player, S_NEED_KEY, "  The rich cache needs a {} to open."), sObjectMgr->GetItemTemplate(cfg.keyItem) ? sObjectMgr->GetItemTemplate(cfg.keyItem)->Name1 : "key");
                else if (!cfg.keyItem && cfg.keys && !wallet.second)
                    h.PSendSysMessage(Text(player, S_NEED_VKEY, "  The rich cache needs an Expedition Key ({}/{} shards)."), wallet.first, cfg.shardsPerKey);
                else
                {
                    if (cfg.keyItem) player->DestroyItemCount(cfg.keyItem, 1, true);
                    else if (cfg.keys) SaveWallet(me, wallet.first, wallet.second - 1);
                    uint32 extra = uint32(t.gold * cfg.richGold);
                    if (extra) player->ModifyMoney(extra);
                    h.PSendSysMessage(Text(player, S_RICH_REWARD, "  Rich cache: {} gold."), extra / 10000);
                    give(cfg.richItem, cfg.richCount);
                    CharacterDatabase.Execute("INSERT INTO mod_temucraft_expedition_progress (owner, map, best_tier, completions, rich_day) VALUES ({}, {}, 0, 0, {}) "
                        "ON DUPLICATE KEY UPDATE rich_day = VALUES(rich_day)", me, run.map, day);
                }
            }
        }
        if (cfg.shardsPerTier)
        {
            auto [shards, keys] = Wallet(me);
            shards += cfg.shardsPerTier * run.tier;
            uint32 made = shards / cfg.shardsPerKey;
            shards %= cfg.shardsPerKey;
            SaveWallet(me, shards, keys + made);
            h.PSendSysMessage(Text(player, S_SHARDS, "  Cache Key Fragments: +{} ({}/{})."), cfg.shardsPerTier * run.tier, shards, cfg.shardsPerKey);
            if (made) h.PSendSysMessage(Text(player, S_KEY_MADE, "  You restore a Cache Key! Keys: {}."), keys + made);
        }
        uint32 best = BestTier(player, run.map);
        CharacterDatabase.Execute("INSERT INTO mod_temucraft_expedition_progress (owner, map, best_tier, completions, rich_day) VALUES ({}, {}, {}, 1, 0) "
            "ON DUPLICATE KEY UPDATE best_tier = GREATEST(best_tier, VALUES(best_tier)), completions = completions + 1", me, run.map, run.tier);
        if (run.tier > best && run.tier < MaxTier())
            h.PSendSysMessage(Text(player, S_TIER_UNLOCKED, "  Tier {} unlocked for this expedition."), run.tier + 1);
    }

    void Complete(Creature* boss, Run& run)
    {
        run.done = true;
        if (GameObject* go = boss->SummonGameObject(GO_CACHE, boss->GetPositionX(), boss->GetPositionY(), boss->GetPositionZ(), boss->GetOrientation(), 0, 0, 0, 0, 3600, true, GO_SUMMON_TIMED_DESPAWN))
        {
            go->SetPhaseMask(cfg.phase, true);
            run.cache = go->GetGUID();
        }
        boss->GetMap()->DoForAllPlayers([&run](Player* p)
        {
            if (run.members.count(p->GetGUID().GetCounter()))
                ChatHandler(p->GetSession()).SendSysMessage(Text(p, S_BOSS_DOWN, "|cffffcc00Expedition complete!|r Open the Expedition Cache for your rewards."));
        });
    }

    void Fail(Map* map, Run& run)
    {
        run.failed = true;
        map->DoForAllPlayers([&run](Player* p)
        {
            if (run.members.count(p->GetGUID().GetCounter()))
                ChatHandler(p->GetSession()).SendSysMessage(Text(p, S_FAILED, "|cffffcc00Expedition failed:|r you ran out of lives. Use .expedition leave to go back."));
        });
        Despawn(map, run);
    }

    // ------------------------------------------------------------------------------------------- the keeper
    enum Actions : uint32
    {
        A_MAIN = 1, A_RICH = 2, A_NOOP = 3,
        A_LIST_BASE = 1000,     // + category * 100 + page
        A_TIER_BASE = 3000,     // + tier
        A_EXPEDITION_BASE = 100000   // + map
    };
    constexpr uint32 PAGE = 14;

    void ShowMain(Player* player, Creature* keeper)
    {
        ClearGossipMenuFor(player);
        std::vector<uint32> rich = RichToday();
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, Acore::StringFormat(Text(player, S_RICH_HEAD, "Today's rich expeditions ({})"), rich.size()), GOSSIP_SENDER_MAIN, A_RICH);
        uint32 counts[MAX_CATEGORY] = {};
        for (auto const& [id, d] : expeditions) if (d.enabled) ++counts[d.category];
        for (uint8 c = 0; c < MAX_CATEGORY; ++c)
            if (counts[c])
                AddGossipItemFor(player, GOSSIP_ICON_BATTLE, Acore::StringFormat(Text(player, S_CATEGORY, "{} ({})"), CATEGORY_NAMES[c], counts[c]), GOSSIP_SENDER_MAIN, A_LIST_BASE + c * 100);
        uint8 affixes = WeeklyAffixes(std::max<uint32>(cfg.affixSecond, cfg.affixFrom));
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, affixes ? Acore::StringFormat(Text(player, S_AFFIXES, "This week's affixes: {} (from tier {}, both from tier {})"), AffixNames(affixes), cfg.affixFrom, cfg.affixSecond)
            : std::string(Text(player, S_NO_AFFIXES, "No affixes this week.")), GOSSIP_SENDER_MAIN, A_MAIN);
        SendGossipMenuFor(player, TEXT_KEEPER, keeper->GetGUID());
    }

    std::string ExpeditionLine(Player* player, Expedition const& d, bool rich)
    {
        uint32 best = BestTier(player, d.map);
        return Acore::StringFormat(Text(player, S_EXPEDITION_LINE, "{}{} - level {}+, best tier {}"), d.name,
            rich ? std::string(Text(player, S_RICH_MARK, " |cff996600(rich)|r")) : std::string(),
            std::max<uint32>(cfg.minLevel, d.minLevel), best);
    }

    void ShowList(Player* player, Creature* keeper, uint8 category, uint32 page)
    {
        ClearGossipMenuFor(player);
        std::vector<uint32> rich = RichToday();
        std::vector<Expedition const*> list;
        for (auto const& [id, d] : expeditions) if (d.enabled && d.category == category) list.push_back(&d);
        std::sort(list.begin(), list.end(), [](Expedition const* a, Expedition const* b) { return a->name < b->name; });
        for (uint32 i = page * PAGE; i < list.size() && i < (page + 1) * PAGE; ++i)
            AddGossipItemFor(player, GOSSIP_ICON_TAXI, ExpeditionLine(player, *list[i], std::find(rich.begin(), rich.end(), list[i]->map) != rich.end()),
                GOSSIP_SENDER_MAIN, A_EXPEDITION_BASE + list[i]->map);
        if ((page + 1) * PAGE < list.size())
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_NEXT, "More..."), GOSSIP_SENDER_MAIN, A_LIST_BASE + category * 100 + page + 1);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_BACK, "Back"), GOSSIP_SENDER_MAIN, A_MAIN);
        SendGossipMenuFor(player, TEXT_KEEPER, keeper->GetGUID());
    }

    void ShowRich(Player* player, Creature* keeper)
    {
        ClearGossipMenuFor(player);
        std::vector<uint32> b = RichToday();
        if (b.empty()) AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_NOTHING_TODAY, "No rich expeditions today."), GOSSIP_SENDER_MAIN, A_MAIN);
        for (uint32 map : b) AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, ExpeditionLine(player, expeditions.at(map), true), GOSSIP_SENDER_MAIN, A_EXPEDITION_BASE + map);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_BACK, "Back"), GOSSIP_SENDER_MAIN, A_MAIN);
        SendGossipMenuFor(player, TEXT_KEEPER, keeper->GetGUID());
    }

    void ShowTiers(Player* player, Creature* keeper, uint32 map)
    {
        ClearGossipMenuFor(player);
        viewing[player->GetGUID().GetCounter()] = map;
        uint32 unlocked = cfg.unlockAll ? MaxTier() : std::min(MaxTier(), BestTier(player, map) + 1);
        for (auto const& [n, t] : tiers)
        {
            if (n > unlocked) { AddGossipItemFor(player, GOSSIP_ICON_CHAT, Acore::StringFormat(Text(player, S_TIER_LOCKED, "Tier {} - finish tier {} first"), n, n - 1), GOSSIP_SENDER_MAIN, A_NOOP); break; }
            uint8 affixes = WeeklyAffixes(n);
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, Acore::StringFormat(Text(player, S_TIER_LINE, "Tier {}: {} lives, {} gold{}"), n,
                t.lives ? std::to_string(t.lives) : std::string("unlimited"), t.gold / 10000,
                affixes ? Acore::StringFormat(" - {}", AffixNames(affixes)) : std::string()), GOSSIP_SENDER_MAIN, A_TIER_BASE + n);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_BACK, "Back"), GOSSIP_SENDER_MAIN, A_LIST_BASE + expeditions.at(map).category * 100);
        SendGossipMenuFor(player, TEXT_KEEPER, keeper->GetGUID());
    }

    class npc_temucraft_expedition_keeper : public CreatureScript
    {
    public:
        npc_temucraft_expedition_keeper() : CreatureScript("npc_temucraft_expedition_keeper") { }

        bool OnGossipHello(Player* player, Creature* creature) override
        {
            if (!cfg.enable) { ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_OFF, "Expeditions are closed.")); return true; }
            ShowMain(player, creature);
            return true;
        }

        bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
        {
            if (!cfg.enable) { CloseGossipMenuFor(player); return true; }
            if (action == A_MAIN || action == A_NOOP) { ShowMain(player, creature); return true; }
            if (action == A_RICH) { ShowRich(player, creature); return true; }
            if (action >= A_EXPEDITION_BASE)
            {
                if (expeditions.count(action - A_EXPEDITION_BASE)) ShowTiers(player, creature, action - A_EXPEDITION_BASE);
                else ShowMain(player, creature);
                return true;
            }
            if (action > A_TIER_BASE)
            {
                uint32 tier = action - A_TIER_BASE;
                auto v = viewing.find(player->GetGUID().GetCounter());
                uint32 unlocked = v == viewing.end() ? 0 : cfg.unlockAll ? MaxTier() : std::min(MaxTier(), BestTier(player, v->second) + 1);
                CloseGossipMenuFor(player);
                if (v != viewing.end() && tiers.count(tier) && tier <= unlocked) Enter(player, v->second, tier, false);
                return true;
            }
            if (action >= A_LIST_BASE)
            {
                uint32 a = action - A_LIST_BASE;
                ShowList(player, creature, uint8(a / 100), a % 100);
                return true;
            }
            ShowMain(player, creature);
            return true;
        }
    };

    class go_temucraft_expedition_cache : public GameObjectScript
    {
    public:
        go_temucraft_expedition_cache() : GameObjectScript("go_temucraft_expedition_cache") { }

        bool OnGossipHello(Player* player, GameObject* go) override
        {
            ClearGossipMenuFor(player);
            Run* run = RunFor(player);
            if (run && run->done && !run->claimed.count(player->GetGUID().GetCounter()))
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, Text(player, S_OPEN, "Open the cache"), GOSSIP_SENDER_MAIN, 1);
            AddGossipItemFor(player, GOSSIP_ICON_TAXI, Text(player, S_LEAVE, "Leave the expedition"), GOSSIP_SENDER_MAIN, 2);
            SendGossipMenuFor(player, TEXT_CACHE, go->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, GameObject* /*go*/, uint32 /*sender*/, uint32 action) override
        {
            CloseGossipMenuFor(player);
            if (action == 1) { if (Run* run = RunFor(player)) Reward(player, *run); }
            else if (action == 2) Leave(player);
            return true;
        }
    };

    // ------------------------------------------------------------------------------------------- hooks
    class temucraft_expeditions_player : public PlayerScript
    {
    public:
        temucraft_expeditions_player() : PlayerScript("temucraft_expeditions_player",
            { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_ON_PLAYER_JUST_DIED, PLAYERHOOK_ON_DELETE }) { }

        void OnPlayerLogin(Player* player) override { if (cfg.enable) Arrive(player); }
        void OnPlayerMapChanged(Player* player) override { if (cfg.enable) Arrive(player); }
        void OnPlayerLogout(Player* player) override
        {
            // the phase isn't saved; at login inside the instance Arrive() puts a member back in the expedition phase
            ObjectGuid::LowType me = player->GetGUID().GetCounter();
            pending.erase(me); viewing.erase(me); previousPhase.erase(me);
        }
        void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
        {
            CharacterDatabase.Execute("DELETE FROM mod_temucraft_expedition_progress WHERE owner = {}", guid.GetCounter());
            CharacterDatabase.Execute("DELETE FROM mod_temucraft_expedition_wallet WHERE owner = {}", guid.GetCounter());
        }
        void OnPlayerJustDied(Player* player) override
        {
            Run* run = RunFor(player);
            if (!run || run->done || run->failed) return;
            ++run->deaths;
            if (run->lives && run->deaths > run->lives) { Fail(player->GetMap(), *run); return; }
            Run const& r = *run;
            player->GetMap()->DoForAllPlayers([&r](Player* p)
            {
                if (!r.members.count(p->GetGUID().GetCounter())) return;
                if (r.lives) ChatHandler(p->GetSession()).PSendSysMessage(Text(p, S_DEATH, "|cffffcc00Expedition:|r a life is lost ({} of {} left). Each death lowers the cache."), r.lives - r.deaths, r.lives);
            });
        }
    };

    class temucraft_expeditions_unit : public UnitScript
    {
    public:
        temucraft_expeditions_unit() : UnitScript("temucraft_expeditions_unit", true,
            { UNITHOOK_ON_UNIT_DEATH, UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN, UNITHOOK_MODIFY_PERIODIC_DAMAGE_AURAS_TICK }) { }

        static float Factor(Unit* attacker)
        {
            if (enemies.empty() || !attacker || !attacker->IsCreature()) return 1.0f;
            auto it = enemies.find(attacker->GetGUID());
            return it == enemies.end() ? 1.0f : it->second.second;
        }
        void ModifySpellDamageTaken(Unit* /*target*/, Unit* attacker, int32& damage, SpellInfo const* /*spellInfo*/) override
        {
            float f = Factor(attacker);
            if (f != 1.0f && damage > 0) damage = int32(damage * f);
        }
        void ModifyPeriodicDamageAurasTick(Unit* /*target*/, Unit* attacker, uint32& damage, SpellInfo const* /*spellInfo*/) override
        {
            float f = Factor(attacker);
            if (f != 1.0f) damage = uint32(damage * f);
        }
        void OnUnitDeath(Unit* unit, Unit* /*killer*/) override
        {
            if (enemies.empty() || !unit || !unit->IsCreature()) return;
            auto it = enemies.find(unit->GetGUID());
            if (it == enemies.end()) return;
            uint32 instance = it->second.first;
            enemies.erase(it);
            auto r = runs.find(instance);
            if (r == runs.end() || r->second.failed) return;
            Run& run = r->second;
            ++run.kills;
            if (unit->GetGUID() == run.boss && !run.done) Complete(unit->ToCreature(), run);
        }
    };

    class temucraft_expeditions_world : public WorldScript
    {
    public:
        temucraft_expeditions_world() : WorldScript("temucraft_expeditions_world", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE }) { }
        void OnAfterConfigLoad(bool /*reload*/) override { LoadSettings(); }
        void OnStartup() override { LoadData(); }
        void OnUpdate(uint32 diff) override
        {
            timer += diff;
            if (timer < 60000) return;
            timer = 0;
            // forget runs whose instance is gone, and teleports that never arrived
            for (auto it = runs.begin(); it != runs.end();)
            {
                if (!sMapMgr->FindMap(it->second.map, it->first))
                {
                    for (ObjectGuid const& g : it->second.creatures) enemies.erase(g);
                    it = runs.erase(it);
                }
                else ++it;
            }
            uint32 now = Now();
            for (auto it = pending.begin(); it != pending.end();)
                it = now - it->second.at > 120 ? pending.erase(it) : std::next(it);
            for (auto it = parties.begin(); it != parties.end();)
                it = it->first + 1000 < nextToken ? parties.erase(it) : std::next(it);
        }
    private:
        uint32 timer = 0;
    };

    class temucraft_expeditions_commands : public CommandScript
    {
    public:
        temucraft_expeditions_commands() : CommandScript("temucraft_expeditions_commands") { }

        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable sub =
            {
                { "leave",  HandleLeave,  SEC_PLAYER,        Console::No },
                { "status", HandleStatus, SEC_PLAYER,        Console::No },
                { "start",  HandleStart,  100501 /* SEC_GAMEMASTER -> RBAC */,    Console::No },
                { "list",   HandleList,   100502 /* SEC_GAMEMASTER -> RBAC */,    Console::Yes },
                { "reload", HandleReload, 100503 /* SEC_ADMINISTRATOR -> RBAC */, Console::Yes },
                { "here",   HandleHere,   100504 /* SEC_GAMEMASTER -> RBAC */,    Console::No },
                { "spot",   HandleSpot,   100505 /* SEC_GAMEMASTER -> RBAC */,    Console::No },
                { "unspot", HandleUnspot, 100506 /* SEC_GAMEMASTER -> RBAC */,    Console::No },
                { "spots",  HandleSpots,  100507 /* SEC_GAMEMASTER -> RBAC */,    Console::No },
            };
            static ChatCommandTable root = { { "expedition", sub } };
            return root;
        }

        static bool HandleLeave(ChatHandler* handler)
        {
            Player* p = handler->GetPlayer();
            if (!p) return true;
            if (!RunFor(p) && !previousPhase.count(p->GetGUID().GetCounter())) { handler->SendSysMessage(Text(handler, S_NOT_IN_EXPEDITION, "You are not in an expedition.")); return true; }
            Leave(p);
            return true;
        }
        static bool HandleStatus(ChatHandler* handler)
        {
            Player* p = handler->GetPlayer();
            if (p)
            {
                auto [shards, keys] = Wallet(p->GetGUID().GetCounter());
                handler->PSendSysMessage(Text(handler, S_WALLET, "Expedition Keys: {}, Cache Key Fragments: {}/{}."), keys, shards, cfg.shardsPerKey);
            }
            Run* run = p ? RunFor(p) : nullptr;
            if (!run) { handler->SendSysMessage(Text(handler, S_NOT_IN_EXPEDITION, "You are not in an expedition.")); return true; }
            handler->PSendSysMessage(Text(handler, S_STATUS, "{} - tier {}: {}/{} enemies defeated, {} deaths{}{}"), expeditions.count(run->map) ? expeditions[run->map].name : "?", run->tier,
                run->kills, run->total, run->deaths, run->lives ? Acore::StringFormat(" (of {} lives)", run->lives) : std::string(),
                run->done ? " - complete" : run->failed ? " - failed" : "");
            return true;
        }
        static bool HandleStart(ChatHandler* handler, uint32 map, Optional<uint32> tier)
        {
            Player* p = handler->GetPlayer();
            if (!p) return true;
            Enter(p, map, tier && tiers.count(*tier) ? *tier : tiers.begin()->first, true);
            return true;
        }
        static bool HandleList(ChatHandler* handler)
        {
            for (auto const& [id, d] : expeditions)
                handler->PSendSysMessage("{} {} [{}] {} spots{}{}", id, d.name, CATEGORY_NAMES[d.category], d.points.size(),
                    d.hand.empty() ? "" : ", hand-made layout", d.enabled ? "" : " (off)");
            return true;
        }
        // ---- building an expedition in game (any dungeon map, also ones nobody uses): stand where things go
        static bool OnDungeon(ChatHandler* handler, Player* p)
        {
            MapEntry const* e = p ? sMapStore.LookupEntry(p->GetMapId()) : nullptr;
            if (!e || !e->IsDungeon())
            {
                handler->SendSysMessage("Expeditions are built inside a dungeon map (.tele or .go to it first).");
                return false;
            }
            return true;
        }

        // .expedition here [name]: this map is an expedition, entered where you stand
        static bool HandleHere(ChatHandler* handler, Optional<Tail> name)
        {
            Player* p = handler->GetPlayer();
            if (!OnDungeon(handler, p)) return true;
            std::string n = name ? std::string(*name) : std::string();
            WorldDatabase.EscapeString(n);
            WorldDatabase.DirectExecute("INSERT INTO mod_temucraft_expedition (map, name, enabled, entrance_x, entrance_y, entrance_z, entrance_o) "
                "VALUES ({}, '{}', 1, {}, {}, {}, {}) ON DUPLICATE KEY UPDATE name = IF(VALUES(name) = '', name, VALUES(name)), enabled = 1, "
                "entrance_x = VALUES(entrance_x), entrance_y = VALUES(entrance_y), entrance_z = VALUES(entrance_z), entrance_o = VALUES(entrance_o)",
                p->GetMapId(), n, p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), p->GetOrientation());
            LoadData();
            handler->PSendSysMessage("Map {} is an expedition, entered here. Add spots with .expedition spot normal|elite|boss (target a creature to use its look).",
                p->GetMapId());
            return true;
        }

        // .expedition spot normal|elite|boss [variant]: an enemy here; its creature is the one you target
        static bool HandleSpot(ChatHandler* handler, std::string kind, Optional<uint8> variant)
        {
            Player* p = handler->GetPlayer();
            if (!OnDungeon(handler, p)) return true;
            uint8 k = kind == "boss" ? K_BOSS : kind == "elite" ? K_ELITE : kind == "normal" || kind == "mob" ? K_MOB : 255;
            Creature* target = handler->getSelectedCreature();
            if (k == 255 || !target)
            {
                handler->SendSysMessage("Target a creature (its look and spells are used), then: .expedition spot normal|elite|boss [variant 0-2].");
                return true;
            }
            uint8 v = variant ? std::min<uint8>(*variant, 2) : 0;
            WorldDatabase.DirectExecute("INSERT INTO mod_temucraft_expedition_spawn (map, variant, kind, entry, x, y, z, o) VALUES ({}, {}, {}, {}, {}, {}, {}, {})",
                p->GetMapId(), v, k, target->GetEntry(), p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), p->GetOrientation());
            LoadData();
            handler->PSendSysMessage("Spot added ({} {}, variant {}). .expedition spots shows the layout.", kind, target->GetName(), v);
            return true;
        }

        // .expedition unspot: remove the nearest hand-made spot within 10 yards
        static bool HandleUnspot(ChatHandler* handler)
        {
            Player* p = handler->GetPlayer();
            if (!OnDungeon(handler, p)) return true;
            QueryResult r = WorldDatabase.Query("SELECT id, POW(x - {0}, 2) + POW(y - {1}, 2) + POW(z - {2}, 2) AS d FROM mod_temucraft_expedition_spawn "
                "WHERE map = {3} HAVING d <= 100 ORDER BY d LIMIT 1", p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), p->GetMapId());
            if (!r) { handler->SendSysMessage("No spot within 10 yards."); return true; }
            WorldDatabase.DirectExecute("DELETE FROM mod_temucraft_expedition_spawn WHERE id = {}", r->Fetch()[0].Get<uint32>());
            LoadData();
            handler->SendSysMessage("Spot removed.");
            return true;
        }

        static bool HandleSpots(ChatHandler* handler)
        {
            Player* p = handler->GetPlayer();
            if (!OnDungeon(handler, p)) return true;
            QueryResult r = WorldDatabase.Query("SELECT variant, SUM(kind = 0), SUM(kind = 1), SUM(kind = 2) FROM mod_temucraft_expedition_spawn WHERE map = {} GROUP BY variant", p->GetMapId());
            if (!r) { handler->SendSysMessage("No hand-made spots on this map yet."); return true; }
            do
            {
                Field* f = r->Fetch();
                handler->PSendSysMessage("Variant {}: {} normal, {} elite, {} boss", f[0].Get<uint8>(), f[1].Get<uint32>(), f[2].Get<uint32>(), f[3].Get<uint32>());
            } while (r->NextRow());
            handler->PSendSysMessage(expeditions.count(p->GetMapId()) ? "This map is a usable expedition." :
                "Not usable yet: it needs .expedition here (an entrance) and at least one spot.");
            return true;
        }

        static bool HandleReload(ChatHandler* handler)
        {
            LoadData();
            handler->PSendSysMessage("mod-temucraft-expeditions: {} expeditions, {} tiers.", expeditions.size(), tiers.size());
            return true;
        }
    };
}

void AddTemucraftExpeditionsScripts()
{
    new temucraft_expeditions_world();
    new npc_temucraft_expedition_keeper();
    new go_temucraft_expedition_cache();
    new temucraft_expeditions_player();
    new temucraft_expeditions_unit();
    new temucraft_expeditions_commands();
}
