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
 * - Nemesis (Temucraft.Expeditions.Nemesis.*): a seasonal boss (creature 608900) that can ambush a run from a tier on,
 *   can be called with a Nemesis Lure (608911) found in caches, and has its own lair with two hidden tiers (? and ??,
 *   `mod_temucraft_expedition_nemesis_tier`).
 * - Relics (`mod_temucraft_expedition_relic`): one combat and one utility relic per character, ranks 1 to 4, bought and
 *   upgraded with Wayfinder Coins (608910). Their effects are stock 3.3.5 auras with our own amounts, on the player or
 *   the companion, and only inside an expedition.
 * - Expedition powers (`mod_temucraft_expedition_power`): run buffs from Strange Disturbances (608902) and chests
 *   (608903) inside; they stack during the run and are cleared when you leave it.
 * - Wayfinder's Ledger (`mod_temucraft_expedition_ledger_rank`): a seasonal track, points from caches by tier and from the
 *   nemesis, with a reward per rank. The Wayfinder Quartermaster (608901) sells for Wayfinder Coins
 *   (`mod_temucraft_expedition_vendor`) and looks after relics.
 * - Bounty maps (608912): one a week; used in a run, a Hidden Trove (608904) appears next to the cache.
 * - Gravestones (Temucraft.Expeditions.Gravestone.*): a player who dies in a run leaves a gravestone (608905) where they
 *   fell; a party member can use it to raise them there, a few times per run.
 * - Slayer enemies (Temucraft.Expeditions.Slayer.*): from a tier, a chance per run that one normal enemy is a tougher
 *   "slayer" that carries extra Wayfinder Coins.
 * - Addon window (Temucraft.Expeditions.Addon.Enable, prefix TEX): the TemucraftExpeditions window shows the current run,
 *   its powers, the equipped relics and the ledger (addon/TemucraftExpeditions, also built into the client by
 *   mod-temucraft-client-version).
 *
 * Commands: .temucraft expedition leave | status | powers | relics | ledger ; GM: .temucraft expedition start <map> <tier>, .temucraft expedition list,
 * .temucraft expedition nemesis ; admin: .temucraft expedition reload
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
#include "Item.h"
#include "ItemScript.h"
#include "Mail.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "TemucraftAddonMessage.h"
#include "TemucraftModuleText.h"
#include "Tokenize.h"
#include "UnitScript.h"
#include "World.h"
#include "WorldScript.h"
#include <algorithm>
#include <map>
#include <mutex>
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
    // 608900-608939 is this module's second id block (see the README)
    constexpr uint32 NPC_QUARTERMASTER = 608901, GO_DISTURBANCE = 608902, GO_POWER_CHEST = 608903, GO_TROVE = 608904;
    constexpr uint32 TEXT_QUARTERMASTER = 608901, TEXT_DISTURBANCE = 608902, TEXT_POWER_CHEST = 608903, TEXT_TROVE = 608904;
    constexpr char const* ADDON_PREFIX = "TEX";   // the TemucraftExpeditions window

    enum Str : uint32
    {
        S_OFF = 1, S_RICH_HEAD, S_CATEGORY, S_AFFIXES, S_NO_AFFIXES, S_BACK, S_NEXT, S_EXPEDITION_LINE, S_TIER_LINE,
        S_TIER_LOCKED, S_LEVEL, S_LEADER, S_TOO_MANY, S_RAID, S_ENTER, S_BOSS_DOWN, S_DEATH, S_FAILED, S_OPEN, S_LEAVE,
        S_NOT_DONE, S_CLAIMED, S_REWARD_GOLD, S_REWARD_ITEM, S_RICH_REWARD, S_NEED_KEY, S_NOT_IN_EXPEDITION, S_STATUS,
        S_NO_EXPEDITION, S_RICH_MARK, S_ENEMIES_LEFT, S_NOTHING_TODAY, S_TIER_UNLOCKED, S_BUSY_ELSEWHERE, S_SHARDS, S_KEY_MADE, S_NEED_VKEY, S_WALLET,
        S_LAIR_LINE = 40, S_LAIR_LOCKED, S_LAIR_LOCKED2, S_NEMESIS_AMBUSH, S_NEMESIS_SUMMONED, S_NEMESIS_DOWN, S_LURE_NO,
        S_POWER_GAIN, S_POWER_HEAD, S_POWER_LINE, S_NO_POWERS, S_DISTURBANCE_PICK, S_DISTURBANCE_USED, S_RELICS,
        S_RELIC_EQUIPPED, S_RELIC_LINE, S_RELIC_LOCKED, S_RELIC_EQUIP, S_RELIC_UPGRADE, S_RELIC_UNLOCK, S_NOT_ENOUGH,
        S_RELIC_UPGRADED, S_RELIC_FOUND, S_RELIC_NOW_EQUIPPED, S_LEDGER, S_LEDGER_RANK, S_LEDGER_POINTS, S_LEDGER_RANK_UP,
        S_VENDOR, S_VENDOR_LINE, S_VENDOR_BOUGHT, S_VENDOR_NEED_RANK, S_VENDOR_CONFIRM, S_BOUNTY_USED, S_BOUNTY_WEEK,
        S_BOUNTY_NO, S_TROVE_OPEN, S_TROVE_NOT_YOURS, S_TROVE_APPEARS, S_SLOT_COMBAT, S_SLOT_UTILITY, S_NONE,
        S_POWER_CHEST_OPEN, S_LEDGER_NOTHING, S_MAIL_SUBJECT, S_MAIL_BODY, S_POWERS_HINT, S_LEDGER_DONE, S_RELIC_MAXED,
        S_BAGS_FULL, S_LEDGER_NEXT, S_RELIC_EQUIPPED_MARK, S_RELIC_NOW_UNEQUIPPED, S_RELIC_UNEQUIP,
        S_WALLET_COINS,
        S_GRAVE_PLACED = 95, S_GRAVE_RAISE, S_GRAVE_RAISED, S_GRAVE_AWAY, S_GRAVE_OWN, S_GRAVE_NOBODY, S_SLAYER_NAME,
        S_SLAYER_APPEARS, S_SLAYER_DOWN
    };

    char const* const CATEGORY_NAMES[] = { "Classic dungeons", "Burning Crusade dungeons", "Wrath of the Lich King dungeons",
                                           "Cataclysm dungeons", "Mists of Pandaria dungeons", "Scenarios", "Temucraft and other dungeons" };
    constexpr uint8 MAX_CATEGORY = 7;
    enum Affix : uint8 { AF_FORTIFIED = 1, AF_TYRANNICAL = 2, AF_BRUTAL = 4, AF_SWARMING = 8 };
    char const* const AFFIX_NAMES[] = { "Hardened", "Warlord's", "Brutal", "Swarming" };
    enum Kind : uint8 { K_MOB = 0, K_ELITE = 1, K_BOSS = 2, K_CACHE = 3, K_NEMESIS = 4 };

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

        // seasonal currency and Wayfinder's Ledger
        uint32 season = 1, coinItem = 608910, coinsPerTier = 5;
        bool ledger = true, vendor = true;
        uint32 ledgerPerTier = 10;
        // nemesis
        bool nemesis = true;
        uint32 nemesisEntry = 608900, nemesisDisplay = 0, ambushFrom = 6, ambushChance = 15, ambushAt = 50;
        float nemesisHealth = 1.5f, nemesisDamage = 1.2f;
        std::vector<std::pair<uint32, uint32>> nemesisSpells;   // spell, seconds between casts
        uint32 enrageSpell = 8599, enrageAt = 30, lureItem = 608911, lureChance = 10, nemesisCoins = 50, nemesisItem = 0,
               nemesisCount = 1, nemesisPoints = 50, lairMap = 33, lairUnlock = 8;
        // relics
        bool relics = true;
        uint32 relicChance = 10, absorbRenew = 20, maxTakenReduction = 50;
        std::vector<uint32> companionEntries = { 608810, 608811, 608812 };
        float companionRange = 100.0f;
        // expedition powers
        bool powers = true;
        uint32 disturbances = 2, chests = 2, choices = 3, chestCoins = 3;
        // bounty maps and Hidden Troves
        bool bounty = true;
        uint32 bountyItem = 608912, bountyChance = 25, troveCoins = 40, troveGold = 500000, troveItem = 0, troveCount = 1,
               troveRelicChance = 25;
        // gravestones
        bool grave = true, graveLair = true;
        uint32 graveEntry = 608905, graveUses = 1, graveFromTier = 1, graveToTier = 0, graveSeconds = 600;
        float graveRestore = 0.5f;
        // slayer enemies
        bool slayer = true;
        uint32 slayerFromTier = 3, slayerChance = 25, slayerCoins = 10, slayerPoints = 0;
        float slayerHealth = 3.0f, slayerDamage = 1.5f, slayerScale = 1.25f;
        // the addon window
        bool addon = true;
    } cfg;

    // what a relic or an expedition power does; the amount is per rank or per stack
    enum Effect : uint8 { E_NONE = 0, E_DAMAGE = 1, E_TAKEN = 2, E_ABSORB = 3, E_HEALING = 4, E_SPEED = 5, E_COINS = 6, E_LUCK = 7 };
    enum Target : uint8 { T_PLAYER = 0, T_COMPANION = 1 };
    enum Slot : uint8 { SLOT_COMBAT = 0, SLOT_UTILITY = 1 };
    constexpr uint8 MAX_RELIC_RANK = 4;
    struct Relic { uint32 id = 0; uint8 slot = 0, effect = 0, target = 0; uint32 spell = 0; std::string name, text; int32 value[MAX_RELIC_RANK] = {}; uint32 cost[MAX_RELIC_RANK] = {}; };
    struct Power { uint32 id = 0; uint8 effect = 0, target = 0; uint32 spell = 0; std::string name, text; int32 value = 0; uint32 maxStacks = 0, weight = 1; };
    struct LedgerRank { uint32 rank = 0, points = 0, item = 0, count = 0, coins = 0, gold = 0; };
    struct VendorItem { uint32 id = 0, item = 0, count = 1, cost = 0, rank = 0; };
    struct NemesisTier { uint32 hidden = 0, baseTier = 1, item = 0, count = 0, coins = 0, points = 0; float health = 1, damage = 1; };
    struct CharState
    {
        uint32 combat = 0, utility = 0, bountyWeek = 0;
        std::map<uint32, uint8> relics;   // relic -> rank
        uint32 points = 0, rank = 0, nemesisKills = 0, lairBest = 0;
    };

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
    struct Party { uint32 count = 1; uint8 level = 1; };
    struct Run
    {
        uint32 map = 0, instance = 0, token = 0, tier = 1, lives = 0, deaths = 0, kills = 0, total = 0, started = 0;
        uint8 affixes = 0;
        bool done = false, failed = false, rich = false;
        ObjectGuid boss, cache;
        std::vector<ObjectGuid> creatures;
        std::set<ObjectGuid::LowType> members, claimed;
        // nemesis: lair = hidden tier (1 = ?, 2 = ??) when this is the nemesis lair; ambushAt = kills before the ambush
        Party party;
        uint8 lair = 0;
        uint32 ambushAt = 0;
        bool lureUsed = false;
        ObjectGuid nemesis, trove;
        // expedition powers: the objects that give them, who used which, and each member's stacks
        std::vector<ObjectGuid> objects;
        std::map<ObjectGuid, std::set<ObjectGuid::LowType>> used;
        std::unordered_map<ObjectGuid::LowType, std::map<uint32, uint32>> powers;
        std::set<ObjectGuid::LowType> troveFor, troveClaimed;
        // gravestones: the object -> who fell there, and how often each member was raised by one this run
        std::map<ObjectGuid, ObjectGuid::LowType> graves;
        std::unordered_map<ObjectGuid::LowType, uint32> graveUses;
        // normal enemies (slayer candidates) and this run's slayer
        std::vector<ObjectGuid> normals;
        ObjectGuid slayer;
    };
    struct Pending { uint32 map = 0, tier = 1, token = 0, at = 0; uint8 lair = 0; WorldLocation from; };

    std::map<uint32, Expedition> expeditions;                                // by map id
    std::map<uint32, Tier> tiers;                                  // by tier
    std::unordered_map<uint32, Run> runs;                          // by instance id
    std::unordered_map<ObjectGuid::LowType, Pending> pending;      // by player
    std::unordered_map<ObjectGuid::LowType, WorldLocation> returnTo;
    std::unordered_map<ObjectGuid::LowType, uint32> previousPhase;
    std::unordered_map<ObjectGuid::LowType, uint32> viewing;       // expedition chosen at the keeper
    std::unordered_map<uint32, Party> parties;                     // by token
    // (instance id, creature) -> (instance, spell damage factor). Keyed by the instance too: every instance copy numbers
    // its creatures from 1, so two runs of the same dungeon summon creatures with the same ObjectGuid.
    using EnemyKey = std::pair<uint32, ObjectGuid>;
    std::map<EnemyKey, std::pair<uint32, float>> enemies;
    EnemyKey EnemyOf(Unit const* u) { return { u->GetInstanceId(), u->GetGUID() }; }
    uint32 nextToken = 1;

    std::map<uint32, Relic> relicList;                             // by id
    std::map<uint32, Power> powerList;                             // by id
    std::map<uint32, LedgerRank> ledgerRanks;                      // by rank
    std::vector<VendorItem> vendorItems;
    std::map<uint32, NemesisTier> nemesisTiers;                    // by hidden tier (1, 2)
    std::set<uint32> auraSpells, absorbSpells, takenSpells;        // every spell a relic or power uses
    std::map<ObjectGuid::LowType, CharState> chars;                // loaded on first use, dropped at logout
    std::mutex charsLock;
    // One lock for all of the module's shared state above: every hook, command and spell handler takes it first, so
    // the module stays safe with MapUpdate.Threads above 1 (maps update in parallel). Recursive: hooks can nest.
    std::recursive_mutex expeditionsLock;
    struct ExpeditionsGuardT { std::lock_guard<std::recursive_mutex> g{ expeditionsLock }; };
    // (instance id or 0 for players, unit) -> when its relic / power shield was last put up
    std::map<EnemyKey, uint32> absorbAt;
    EnemyKey AbsorbKeyOf(Unit const* u) { return { u->IsPlayer() ? 0 : u->GetInstanceId(), u->GetGUID() }; }

    template<typename T>
    char const* Text(T who, uint32 id, char const* fallback) { return TemucraftModuleText(MODULE, who, id, fallback); }

    uint32 Now() { return uint32(GameTime::GetGameTime().count()); }
    uint32 Day() { return Now() / 86400; }
    uint32 Week() { return (Now() + 3 * 86400) / 604800; }

    void Notify(Player* player);   // the addon window (further down)

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

        cfg.season = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Season", 1);
        cfg.coinItem = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Currency.Item", 608910);
        cfg.coinsPerTier = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Currency.PerTier", 5);
        cfg.ledger = sConfigMgr->GetOption<bool>("Temucraft.Expeditions.Ledger.Enable", true);
        cfg.ledgerPerTier = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Ledger.PointsPerTier", 10);
        cfg.vendor = sConfigMgr->GetOption<bool>("Temucraft.Expeditions.Vendor.Enable", true);

        cfg.nemesis = sConfigMgr->GetOption<bool>("Temucraft.Expeditions.Nemesis.Enable", true);
        cfg.nemesisEntry = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.Entry", 608900);
        cfg.nemesisDisplay = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.DisplayId", 0);
        cfg.ambushFrom = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.AmbushFromTier", 6);
        cfg.ambushChance = std::min<uint32>(100, sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.AmbushChance", 15));
        cfg.ambushAt = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.AmbushAtPercent", 50), 1, 100);
        cfg.nemesisHealth = sConfigMgr->GetOption<float>("Temucraft.Expeditions.Nemesis.Health", 1.5f);
        cfg.nemesisDamage = sConfigMgr->GetOption<float>("Temucraft.Expeditions.Nemesis.Damage", 1.2f);
        cfg.nemesisSpells.clear();
        std::string spells = sConfigMgr->GetOption<std::string>("Temucraft.Expeditions.Nemesis.Spells", "15496:8 17228:12 22884:30");
        for (std::string_view entry : Acore::Tokenize(spells, ' ', false))
        {
            std::vector<std::string_view> parts = Acore::Tokenize(entry, ':', false);
            Optional<uint32> spell = parts.empty() ? Optional<uint32>() : Acore::StringTo<uint32>(parts[0]);
            Optional<uint32> every = parts.size() > 1 ? Acore::StringTo<uint32>(parts[1]) : Optional<uint32>(10);
            if (spell && *spell && every) cfg.nemesisSpells.emplace_back(*spell, std::max<uint32>(2, *every));
        }
        cfg.enrageSpell = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.EnrageSpell", 8599);
        cfg.enrageAt = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.EnrageAt", 30);
        cfg.lureItem = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.LureItem", 608911);
        cfg.lureChance = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.LureDropChance", 10);
        cfg.nemesisCoins = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.Coins", 50);
        cfg.nemesisItem = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.RewardItem", 0);
        cfg.nemesisCount = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.RewardCount", 1);
        cfg.nemesisPoints = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.LedgerPoints", 50);
        cfg.lairMap = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.LairMap", 33);
        cfg.lairUnlock = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Nemesis.LairUnlockTier", 8);

        cfg.relics = sConfigMgr->GetOption<bool>("Temucraft.Expeditions.Relics.Enable", true);
        cfg.relicChance = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Relics.DropChance", 10);
        cfg.absorbRenew = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Relics.ShieldRenewSeconds", 20));
        cfg.maxTakenReduction = std::min<uint32>(95, sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Relics.MaxDamageTakenReduction", 50));
        cfg.companionEntries.clear();
        std::string companions = sConfigMgr->GetOption<std::string>("Temucraft.Expeditions.Relics.CompanionEntries", "608810 608811 608812");
        for (std::string_view entry : Acore::Tokenize(companions, ' ', false))
            if (Optional<uint32> id = Acore::StringTo<uint32>(entry)) cfg.companionEntries.push_back(*id);
        cfg.companionRange = sConfigMgr->GetOption<float>("Temucraft.Expeditions.Relics.CompanionRange", 100.0f);

        cfg.powers = sConfigMgr->GetOption<bool>("Temucraft.Expeditions.Powers.Enable", true);
        cfg.disturbances = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Powers.Disturbances", 2);
        cfg.chests = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Powers.Chests", 2);
        cfg.choices = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Powers.Choices", 3), 1, 10);
        cfg.chestCoins = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Powers.ChestCoins", 3);

        cfg.bounty = sConfigMgr->GetOption<bool>("Temucraft.Expeditions.Bounty.Enable", true);
        cfg.bountyItem = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Bounty.Item", 608912);
        cfg.bountyChance = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Bounty.DropChance", 25);
        cfg.troveCoins = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Bounty.Trove.Coins", 40);
        cfg.troveGold = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Bounty.Trove.Gold", 500000);
        cfg.troveItem = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Bounty.Trove.Item", 0);
        cfg.troveCount = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Bounty.Trove.ItemCount", 1);
        cfg.troveRelicChance = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Bounty.Trove.RelicChance", 25);

        cfg.grave = sConfigMgr->GetOption<bool>("Temucraft.Expeditions.Gravestone.Enable", true);
        cfg.graveEntry = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Gravestone.Entry", 608905);
        cfg.graveUses = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Gravestone.UsesPerRun", 1);
        cfg.graveFromTier = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Gravestone.FromTier", 1);
        cfg.graveToTier = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Gravestone.ToTier", 0);
        cfg.graveLair = sConfigMgr->GetOption<bool>("Temucraft.Expeditions.Gravestone.InLair", true);
        cfg.graveSeconds = std::max<uint32>(10, sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Gravestone.Seconds", 600));
        cfg.graveRestore = std::clamp(sConfigMgr->GetOption<float>("Temucraft.Expeditions.Gravestone.RestorePercent", 50.0f), 1.0f, 100.0f) / 100.0f;

        cfg.slayer = sConfigMgr->GetOption<bool>("Temucraft.Expeditions.Slayer.Enable", true);
        cfg.slayerFromTier = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Slayer.FromTier", 3);
        cfg.slayerChance = std::min<uint32>(100, sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Slayer.Chance", 25));
        cfg.slayerHealth = std::max(0.1f, sConfigMgr->GetOption<float>("Temucraft.Expeditions.Slayer.Health", 3.0f));
        cfg.slayerDamage = std::max(0.1f, sConfigMgr->GetOption<float>("Temucraft.Expeditions.Slayer.Damage", 1.5f));
        cfg.slayerScale = std::clamp(sConfigMgr->GetOption<float>("Temucraft.Expeditions.Slayer.Scale", 1.25f), 0.5f, 3.0f);
        cfg.slayerCoins = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Slayer.Coins", 10);
        cfg.slayerPoints = sConfigMgr->GetOption<uint32>("Temucraft.Expeditions.Slayer.LedgerPoints", 0);

        cfg.addon = sConfigMgr->GetOption<bool>("Temucraft.Expeditions.Addon.Enable", true);
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

    // relics, powers, ledger ranks, wares and the nemesis lair tiers
    void LoadFeatureData()
    {
        relicList.clear(); powerList.clear(); ledgerRanks.clear(); vendorItems.clear(); nemesisTiers.clear();
        auraSpells.clear(); absorbSpells.clear(); takenSpells.clear();
        auto useSpell = [](uint32 spell, uint8 effect, char const* what, uint32 id) -> uint32
        {
            if (!spell) return 0;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spell);
            if (!info) { LOG_ERROR("module", "mod-temucraft-expeditions: {} {} uses spell {}, which does not exist", what, id, spell); return 0; }
            // our amounts replace the stock ones; enemies must not dispel or steal them
            const_cast<SpellInfo*>(info)->Dispel = DISPEL_NONE;
            auraSpells.insert(spell);
            if (effect == E_ABSORB) absorbSpells.insert(spell);
            if (effect == E_TAKEN) takenSpells.insert(spell);
            return spell;
        };
        if (QueryResult r = WorldDatabase.Query("SELECT id, slot, name, description, effect, target, spell, value1, value2, value3, value4, "
            "unlock_cost, cost2, cost3, cost4 FROM mod_temucraft_expedition_relic WHERE enabled = 1"))
            do
            {
                Field* f = r->Fetch(); Relic x;
                x.id = f[0].Get<uint32>(); x.slot = f[1].Get<uint8>() ? SLOT_UTILITY : SLOT_COMBAT; x.name = f[2].Get<std::string>(); x.text = f[3].Get<std::string>();
                x.effect = f[4].Get<uint8>(); x.target = f[5].Get<uint8>() ? T_COMPANION : T_PLAYER; x.spell = useSpell(f[6].Get<uint32>(), x.effect, "relic", x.id);
                for (uint8 i = 0; i < MAX_RELIC_RANK; ++i) { x.value[i] = f[7 + i].Get<int32>(); x.cost[i] = f[11 + i].Get<uint32>(); }
                relicList[x.id] = x;
            } while (r->NextRow());
        if (QueryResult r = WorldDatabase.Query("SELECT id, name, description, effect, target, spell, value, max_stacks, weight FROM mod_temucraft_expedition_power WHERE enabled = 1"))
            do
            {
                Field* f = r->Fetch(); Power x;
                x.id = f[0].Get<uint32>(); x.name = f[1].Get<std::string>(); x.text = f[2].Get<std::string>(); x.effect = f[3].Get<uint8>();
                x.target = f[4].Get<uint8>() ? T_COMPANION : T_PLAYER; x.spell = useSpell(f[5].Get<uint32>(), x.effect, "power", x.id);
                x.value = f[6].Get<int32>(); x.maxStacks = f[7].Get<uint32>(); x.weight = std::max<uint32>(1, f[8].Get<uint32>());
                powerList[x.id] = x;
            } while (r->NextRow());
        if (QueryResult r = WorldDatabase.Query("SELECT `rank`, points, item, item_count, coins, gold FROM mod_temucraft_expedition_ledger_rank ORDER BY `rank`"))
            do
            {
                Field* f = r->Fetch(); LedgerRank x;
                x.rank = f[0].Get<uint32>(); x.points = f[1].Get<uint32>(); x.item = f[2].Get<uint32>(); x.count = f[3].Get<uint32>(); x.coins = f[4].Get<uint32>(); x.gold = f[5].Get<uint32>();
                ledgerRanks[x.rank] = x;
            } while (r->NextRow());
        if (QueryResult r = WorldDatabase.Query("SELECT id, item, item_count, cost, ledger_rank FROM mod_temucraft_expedition_vendor WHERE enabled = 1 ORDER BY sort, id"))
            do
            {
                Field* f = r->Fetch(); VendorItem x;
                x.id = f[0].Get<uint32>(); x.item = f[1].Get<uint32>(); x.count = std::max<uint32>(1, f[2].Get<uint32>()); x.cost = f[3].Get<uint32>(); x.rank = f[4].Get<uint32>();
                if (sObjectMgr->GetItemTemplate(x.item)) vendorItems.push_back(x);
                else LOG_ERROR("module", "mod-temucraft-expeditions: ware {} sells item {}, which does not exist", x.id, x.item);
            } while (r->NextRow());
        if (QueryResult r = WorldDatabase.Query("SELECT hidden_tier, base_tier, health, damage, item, item_count, coins, ledger_points FROM mod_temucraft_expedition_nemesis_tier"))
            do
            {
                Field* f = r->Fetch(); NemesisTier x;
                x.hidden = f[0].Get<uint32>(); x.baseTier = f[1].Get<uint32>(); x.health = f[2].Get<float>(); x.damage = f[3].Get<float>();
                x.item = f[4].Get<uint32>(); x.count = f[5].Get<uint32>(); x.coins = f[6].Get<uint32>(); x.points = f[7].Get<uint32>();
                if (x.hidden == 1 || x.hidden == 2) nemesisTiers[x.hidden] = x;
            } while (r->NextRow());
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
        LoadFeatureData();
        LOG_INFO("module", "mod-temucraft-expeditions: {} expeditions, {} tiers, {} relics, {} powers, {} ledger ranks, {} wares",
            expeditions.size(), tiers.size(), relicList.size(), powerList.size(), ledgerRanks.size(), vendorItems.size());
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

    // ------------------------------------------------------------------------------------------- character state
    // relics, the equipped pair, the bounty week and this season's ledger, per character
    CharState& State(Player* player)
    {
        ObjectGuid::LowType me = player->GetGUID().GetCounter();
        std::lock_guard<std::mutex> guard(charsLock);
        auto it = chars.find(me);
        if (it != chars.end()) return it->second;
        CharState& s = chars[me];
        if (QueryResult r = CharacterDatabase.Query("SELECT combat_relic, utility_relic, bounty_week FROM mod_temucraft_expedition_character WHERE owner = {}", me))
        {
            s.combat = r->Fetch()[0].Get<uint32>(); s.utility = r->Fetch()[1].Get<uint32>(); s.bountyWeek = r->Fetch()[2].Get<uint32>();
        }
        if (QueryResult r = CharacterDatabase.Query("SELECT relic, `rank` FROM mod_temucraft_expedition_relic WHERE owner = {}", me))
            do s.relics[r->Fetch()[0].Get<uint32>()] = std::min<uint8>(MAX_RELIC_RANK, r->Fetch()[1].Get<uint8>()); while (r->NextRow());
        if (QueryResult r = CharacterDatabase.Query("SELECT points, `rank`, nemesis_kills, lair_best FROM mod_temucraft_expedition_ledger WHERE owner = {} AND season = {}", me, cfg.season))
        {
            Field* f = r->Fetch();
            s.points = f[0].Get<uint32>(); s.rank = f[1].Get<uint32>(); s.nemesisKills = f[2].Get<uint32>(); s.lairBest = f[3].Get<uint32>();
        }
        return s;
    }
    void SaveChar(ObjectGuid::LowType me, CharState const& s)
    {
        CharacterDatabase.Execute("REPLACE INTO mod_temucraft_expedition_character (owner, combat_relic, utility_relic, bounty_week) VALUES ({}, {}, {}, {})",
            me, s.combat, s.utility, s.bountyWeek);
    }
    void SaveRelic(ObjectGuid::LowType me, uint32 relic, uint8 rank)
    {
        CharacterDatabase.Execute("REPLACE INTO mod_temucraft_expedition_relic (owner, relic, `rank`) VALUES ({}, {}, {})", me, relic, rank);
    }
    void SaveLedger(ObjectGuid::LowType me, CharState const& s)
    {
        CharacterDatabase.Execute("REPLACE INTO mod_temucraft_expedition_ledger (owner, season, points, `rank`, nemesis_kills, lair_best) VALUES ({}, {}, {}, {}, {}, {})",
            me, cfg.season, s.points, s.rank, s.nemesisKills, s.lairBest);
    }

    // ------------------------------------------------------------------------------------------- items and coins
    std::string ItemName(uint32 item)
    {
        ItemTemplate const* t = sObjectMgr->GetItemTemplate(item);
        return t ? t->Name1 : std::string("?");
    }
    std::string CoinName() { return sObjectMgr->GetItemTemplate(cfg.coinItem) ? ItemName(cfg.coinItem) : std::string("Wayfinder Coin"); }

    // into the bags, and whatever does not fit by mail
    void Give(Player* player, uint32 item, uint32 count, bool tell = true)
    {
        if (!item || !count || !sObjectMgr->GetItemTemplate(item)) return;
        uint32 noSpace = 0;
        ItemPosCountVec dest;
        player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item, count, &noSpace);
        uint32 stored = count - std::min(noSpace, count);
        if (stored && !dest.empty())
            if (Item* it = player->StoreNewItem(dest, item, true))
                player->SendNewItem(it, stored, true, false);
        uint32 rest = count - stored;
        if (rest)
        {
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            MailDraft draft(Text(player, S_MAIL_SUBJECT, "Expedition rewards"), Text(player, S_MAIL_BODY, "Your bags were full, so these came by mail."));
            uint32 stack = std::max<uint32>(1, sObjectMgr->GetItemTemplate(item)->GetMaxStackSize());
            for (uint32 left = rest; left;)
            {
                uint32 n = std::min(left, stack);
                if (Item* it = Item::CreateItem(item, n, player)) { it->SaveToDB(trans); draft.AddItem(it); }
                left -= n;
            }
            draft.SendMailTo(trans, MailReceiver(player), MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_DEFAULT));
            CharacterDatabase.CommitTransaction(trans);
            ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_BAGS_FULL, "  Your bags are full: the rest comes by mail."));
        }
        if (tell) ChatHandler(player->GetSession()).PSendSysMessage(Text(player, S_REWARD_ITEM, "  Cache: {} x{}."), ItemName(item), count);
    }
    uint32 CoinCount(Player* player) { return cfg.coinItem ? player->GetItemCount(cfg.coinItem) : 0; }
    bool Pay(Player* player, uint32 cost)
    {
        if (!cost) return true;
        if (!cfg.coinItem || CoinCount(player) < cost)
        {
            ChatHandler(player->GetSession()).PSendSysMessage(Text(player, S_NOT_ENOUGH, "You need {} {} for that."), cost, CoinName());
            return false;
        }
        player->DestroyItemCount(cfg.coinItem, cost, true);
        return true;
    }

    // ------------------------------------------------------------------------------------------- relic and power effects
    std::string EffectText(std::string const& text, int32 value)
    {
        return text.find("{}") != std::string::npos ? Acore::StringFormat(text, value) : text;
    }
    Relic const* RelicById(uint32 id) { auto it = relicList.find(id); return it == relicList.end() ? nullptr : &it->second; }
    Power const* PowerById(uint32 id) { auto it = powerList.find(id); return it == powerList.end() ? nullptr : &it->second; }

    // sum of an effect handled in code (coins, luck) from the equipped relics and this run's powers
    int32 CodeEffect(Player* player, Run const* run, uint8 effect)
    {
        int32 total = 0;
        if (cfg.relics)
        {
            CharState& s = State(player);
            for (uint32 id : { s.combat, s.utility })
                if (Relic const* r = RelicById(id))
                {
                    auto owned = s.relics.find(id);
                    if (r->effect == effect && owned != s.relics.end() && owned->second) total += r->value[owned->second - 1];
                }
        }
        if (cfg.powers && run)
        {
            auto it = run->powers.find(player->GetGUID().GetCounter());
            if (it != run->powers.end())
                for (auto const& [id, stacks] : it->second)
                    if (Power const* p = PowerById(id)) if (p->effect == effect) total += p->value * int32(stacks);
        }
        return total;
    }

    Creature* FindCompanion(Player* player)
    {
        if (cfg.companionEntries.empty() || !player->IsInWorld()) return nullptr;
        std::list<Creature*> list;
        player->GetCreatureListWithEntryInGrid(list, cfg.companionEntries, cfg.companionRange);
        for (Creature* c : list)
            if (c->GetCreatorGUID() == player->GetGUID() && c->IsAlive()) return c;
        return nullptr;
    }

    // put on, update or take off the relic / power auras the player cast on target, to match want (spell -> amount)
    void Apply(Player* caster, Unit* target, std::map<uint32, int32> const& want)
    {
        if (!target || !target->IsInWorld()) return;
        uint32 now = Now();
        for (uint32 spell : auraSpells)
        {
            auto it = want.find(spell);
            Aura* aura = target->GetAura(spell, caster->GetGUID());
            if (it == want.end() || it->second == 0 || !target->IsAlive())
            {
                if (aura) target->RemoveAura(spell, caster->GetGUID());
                continue;
            }
            bool absorb = absorbSpells.count(spell) != 0;
            if (!aura)
            {
                // a used-up shield comes back after Relics.ShieldRenewSeconds
                if (absorb) { auto at = absorbAt.find(AbsorbKeyOf(target)); if (at != absorbAt.end() && now - at->second < cfg.absorbRenew) continue; }
                aura = caster->AddAura(spell, target);
                if (!aura) continue;
                aura->SetMaxDuration(-1);
                aura->SetDuration(-1);
                if (AuraEffect* e = aura->GetEffect(EFFECT_0)) e->ChangeAmount(it->second);
                if (absorb) absorbAt[AbsorbKeyOf(target)] = now;
            }
            else if (!absorb)
                if (AuraEffect* e = aura->GetEffect(EFFECT_0))
                    if (e->GetAmount() != it->second) e->ChangeAmount(it->second);
        }
    }

    // what the player's relics and this run's powers should put on the player and on the companion, now
    void Refresh(Player* player)
    {
        if (auraSpells.empty() || !player->IsInWorld()) return;
        Run* run = RunFor(player);
        std::map<uint32, int32> self, companion;
        if (run)
        {
            auto add = [player, &self, &companion](uint8 effect, uint8 target, uint32 spell, int32 value)
            {
                if (!spell || !value) return;
                Unit* who = player;
                if (target == T_COMPANION) who = FindCompanion(player);
                int32 amount = effect == E_TAKEN ? -value : effect == E_ABSORB ? (who ? int32(who->GetMaxHealth() * uint32(std::max(0, value)) / 100) : 0) : value;
                (target == T_COMPANION ? companion : self)[spell] += amount;
            };
            if (cfg.relics)
            {
                CharState& s = State(player);
                for (uint32 id : { s.combat, s.utility })
                    if (Relic const* r = RelicById(id))
                    {
                        auto owned = s.relics.find(id);
                        if (owned != s.relics.end() && owned->second) add(r->effect, r->target, r->spell, r->value[owned->second - 1]);
                    }
            }
            if (cfg.powers)
            {
                auto it = run->powers.find(player->GetGUID().GetCounter());
                if (it != run->powers.end())
                    for (auto const& [id, stacks] : it->second)
                        if (Power const* p = PowerById(id)) add(p->effect, p->target, p->spell, p->value * int32(stacks));
            }
        }
        // damage taken never drops below MaxDamageTakenReduction
        for (auto* want : { &self, &companion })
            for (auto& [spell, amount] : *want)
                if (takenSpells.count(spell)) amount = std::max(amount, -int32(cfg.maxTakenReduction));
        Apply(player, player, self);
        if (Creature* c = FindCompanion(player)) Apply(player, c, companion);
    }
    // take every relic / power aura off (leaving an expedition, or logging in outside one)
    void Strip(Player* player)
    {
        std::map<uint32, int32> none;
        Apply(player, player, none);
        if (Creature* c = FindCompanion(player)) Apply(player, c, none);
    }

    // ------------------------------------------------------------------------------------------- expedition powers
    // n different powers, weighted, leaving out ones the player already has at their maximum; same seed, same offer
    std::vector<uint32> PowerChoices(Run const& run, ObjectGuid::LowType me, uint32 seed, uint32 n)
    {
        std::vector<Power const*> pool;
        auto mine = run.powers.find(me);
        for (auto const& [id, p] : powerList)
        {
            uint32 have = 0;
            if (mine != run.powers.end()) { auto h = mine->second.find(id); if (h != mine->second.end()) have = h->second; }
            if (!p.maxStacks || have < p.maxStacks) pool.push_back(&p);
        }
        std::mt19937 rng(seed);
        std::vector<uint32> out;
        while (!pool.empty() && out.size() < n)
        {
            uint32 total = 0;
            for (Power const* p : pool) total += p->weight;
            uint32 pick = std::uniform_int_distribution<uint32>(0, total - 1)(rng);
            for (size_t i = 0; i < pool.size(); ++i)
            {
                if (pick < pool[i]->weight) { out.push_back(pool[i]->id); pool.erase(pool.begin() + i); break; }
                pick -= pool[i]->weight;
            }
        }
        return out;
    }

    void GainPower(Player* player, Run& run, uint32 id)
    {
        Power const* p = PowerById(id);
        if (!p) return;
        uint32& stacks = run.powers[player->GetGUID().GetCounter()][id];
        uint32 add = 1;
        // the luck effect: a chance of one more stack
        if (int32 luck = CodeEffect(player, &run, E_LUCK); luck > 0 && roll_chance_i(luck)) add = 2;
        stacks = p->maxStacks ? std::min(p->maxStacks, stacks + add) : stacks + add;
        ChatHandler(player->GetSession()).PSendSysMessage(Text(player, S_POWER_GAIN, "|cff66ccffExpedition power:|r {} x{} ({})."),
            p->name, stacks, EffectText(p->text, p->value * int32(stacks)));
        Refresh(player);
        Notify(player);
    }

    // a random relic the player has not found yet, at rank 1
    void FindRelic(Player* player)
    {
        if (!cfg.relics) return;
        CharState& s = State(player);
        std::vector<uint32> missing;
        for (auto const& [id, r] : relicList) if (!s.relics.count(id)) missing.push_back(id);
        if (missing.empty()) return;
        uint32 id = missing[urand(0, uint32(missing.size()) - 1)];
        s.relics[id] = 1;
        SaveRelic(player->GetGUID().GetCounter(), id, 1);
        ChatHandler(player->GetSession()).PSendSysMessage(Text(player, S_RELIC_FOUND, "  |cffa335eeRelic found:|r {}!"), relicList[id].name);
    }

    // ------------------------------------------------------------------------------------------- Wayfinder's Ledger
    void AddLedger(Player* player, uint32 points)
    {
        if (!cfg.ledger || !points) return;
        CharState& s = State(player);
        s.points += points;
        ChatHandler h(player->GetSession());
        h.PSendSysMessage(Text(player, S_LEDGER_POINTS, "  Wayfinder's Ledger: +{} points ({} in total)."), points, s.points);
        for (auto const& [n, rank] : ledgerRanks)
        {
            if (n <= s.rank) continue;
            if (rank.points > s.points) break;
            s.rank = n;
            h.PSendSysMessage(Text(player, S_LEDGER_RANK_UP, "  |cffffcc00Wayfinder's Ledger rank {}!|r"), n);
            if (rank.gold) { player->ModifyMoney(rank.gold); h.PSendSysMessage(Text(player, S_REWARD_GOLD, "  Cache: {} gold."), rank.gold / 10000); }
            Give(player, rank.item, rank.count);
            Give(player, cfg.coinItem, rank.coins);
        }
        SaveLedger(player->GetGUID().GetCounter(), s);
        Notify(player);
    }

    // ------------------------------------------------------------------------------------------- the addon window
    // BEGIN, then RUN~name~tier~enemies left~total~deaths~lives~state~affixes (state 0 running, 1 done, 2 failed) or
    // NONE, then P~power~stacks~effect per power, R~slot~relic~rank per equipped relic, L~season~rank~points~next rank
    // points~coin name~coins, END. The window asks with HELLO; the server also sends it after anything that changes it.
    void Notify(Player* player)
    {
        if (!cfg.enable || !cfg.addon || !player || !player->IsInWorld()) return;
        TemucraftAddonSend(player, ADDON_PREFIX, "BEGIN");
        Run* run = RunFor(player);
        if (run)
        {
            std::string name = expeditions.count(run->map) ? expeditions[run->map].name : std::string("?");
            std::string tier = run->lair ? std::string(run->lair == 1 ? "?" : "??") : std::to_string(run->tier);
            uint32 left = run->total > run->kills ? run->total - run->kills : 0;
            TemucraftAddonSend(player, ADDON_PREFIX, Acore::StringFormat("RUN~{}~{}~{}~{}~{}~{}~{}~{}", TemucraftAddonClean(name), tier, left, run->total,
                run->deaths, run->lives, run->done ? 1 : run->failed ? 2 : 0, TemucraftAddonClean(run->affixes ? AffixNames(run->affixes) : std::string())));
            if (cfg.powers)
            {
                auto mine = run->powers.find(player->GetGUID().GetCounter());
                if (mine != run->powers.end())
                    for (auto const& [id, stacks] : mine->second)
                        if (Power const* pw = PowerById(id))
                            TemucraftAddonSend(player, ADDON_PREFIX, Acore::StringFormat("P~{}~{}~{}", TemucraftAddonClean(pw->name), stacks,
                                TemucraftAddonClean(EffectText(pw->text, pw->value * int32(stacks)))));
            }
        }
        else
            TemucraftAddonSend(player, ADDON_PREFIX, "NONE");
        CharState& s = State(player);
        if (cfg.relics)
            for (uint32 id : { s.combat, s.utility })
                if (Relic const* r = RelicById(id))
                    if (uint8 rank = s.relics.count(id) ? s.relics[id] : 0)
                        TemucraftAddonSend(player, ADDON_PREFIX, Acore::StringFormat("R~{}~{}~{}", r->slot, TemucraftAddonClean(r->name), rank));
        auto next = ledgerRanks.upper_bound(s.rank);
        TemucraftAddonSend(player, ADDON_PREFIX, Acore::StringFormat("L~{}~{}~{}~{}~{}~{}", cfg.season, cfg.ledger ? s.rank : 0, cfg.ledger ? s.points : 0,
            cfg.ledger && next != ledgerRanks.end() ? next->second.points : 0, TemucraftAddonClean(CoinName()), CoinCount(player)));
        TemucraftAddonSend(player, ADDON_PREFIX, "END");
    }

    // everyone in the run who is on the map
    void NotifyRun(Map* map, Run const& run)
    {
        if (!cfg.addon || !map) return;
        map->DoForAllPlayers([&run](Player* p) { if (run.members.count(p->GetGUID().GetCounter())) Notify(p); });
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
        bool boss = kind == K_BOSS || kind == K_NEMESIS;
        float health = (boss ? cfg.healthBoss : kind == K_ELITE ? cfg.healthElite : cfg.healthMob) * tier.health * (1.0f + cfg.partyHealth * (party.count - 1));
        float damage = tier.damage * (1.0f + cfg.partyDamage * (party.count - 1));
        if ((run.affixes & AF_FORTIFIED) && !boss) health *= 1.25f;
        if ((run.affixes & AF_TYRANNICAL) && boss) { health *= 1.3f; damage *= 1.15f; }
        if (run.affixes & AF_BRUTAL) damage *= 1.15f;
        if (kind == K_NEMESIS)
        {
            health *= cfg.nemesisHealth; damage *= cfg.nemesisDamage;
            auto lt = nemesisTiers.find(run.lair);
            if (run.lair && lt != nemesisTiers.end()) { health *= lt->second.health; damage *= lt->second.damage; }
        }
        float weapon = damage * (boss ? cfg.damageBoss : kind == K_ELITE ? cfg.damageElite : cfg.damageMob);

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
        enemies[EnemyKey(run.instance, c->GetGUID())] = { run.instance, std::clamp(spell, 0.5f, 50.0f) * damage * (boss ? 1.25f : 1.0f) };
    }

    Creature* Place(Map* map, Run& run, uint32 entry, Position const& pos, Kind kind, Party const& party)
    {
        if (!entry || !sObjectMgr->GetCreatureTemplate(entry)) return nullptr;
        map->LoadGrid(pos.GetPositionX(), pos.GetPositionY());
        TempSummon* c = map->SummonCreature(entry, pos);
        if (!c) return nullptr;
        c->SetPhaseMask(cfg.phase, true);
        if (kind == K_NEMESIS && cfg.nemesisDisplay) { c->SetDisplayId(cfg.nemesisDisplay); c->SetNativeDisplayId(cfg.nemesisDisplay); }
        Scale(c, kind, party, run);
        run.creatures.push_back(c->GetGUID());
        if (kind == K_MOB) run.normals.push_back(c->GetGUID());
        return c;
    }

    void Despawn(Map* map, Run& run)
    {
        for (ObjectGuid const& g : run.creatures)
        {
            enemies.erase(EnemyKey(run.instance, g));
            if (map)
                if (Creature* c = map->GetCreature(g)) c->DespawnOrUnsummon();
        }
        run.creatures.clear();
        if (map)
        {
            for (ObjectGuid const& g : run.objects)
                if (GameObject* go = map->GetGameObject(g)) go->Delete();
            for (ObjectGuid const& g : { run.cache, run.trove })
                if (g) if (GameObject* go = map->GetGameObject(g)) go->Delete();
            for (auto const& [g, who] : run.graves)
                if (GameObject* go = map->GetGameObject(g)) go->Delete();
        }
        run.graves.clear();
        run.objects.clear();
        run.cache.Clear();
        run.trove.Clear();
    }

    GameObject* PlaceObject(Map* map, Run& run, uint32 entry, Position const& pos)
    {
        if (!entry || !sObjectMgr->GetGameObjectTemplate(entry)) return nullptr;
        map->LoadGrid(pos.GetPositionX(), pos.GetPositionY());
        GameObject* go = map->SummonGameObject(entry, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), pos.GetOrientation(), 0, 0, 0, 0, 0);
        if (!go) return nullptr;
        go->SetPhaseMask(cfg.phase, true);
        run.objects.push_back(go->GetGUID());
        return go;
    }

    // Strange Disturbances and chests, next to some of the enemies' spots (not the boss)
    void PlacePowerObjects(Map* map, Run& run, std::vector<Position> spots)
    {
        if (!cfg.powers || powerList.empty() || spots.empty()) return;
        std::mt19937 rng(run.instance * 2654435761u + run.started);
        std::shuffle(spots.begin(), spots.end(), rng);
        size_t next = 0;
        auto place = [&](uint32 entry, uint32 count)
        {
            for (uint32 i = 0; i < count && next < spots.size(); ++i, ++next)
            {
                Position p = spots[next];
                p.RelocateOffset({ 3.0f * std::cos(p.GetOrientation()), 3.0f * std::sin(p.GetOrientation()), 0.0f, 0.0f });
                PlaceObject(map, run, entry, p);
            }
        };
        place(GO_DISTURBANCE, cfg.disturbances);
        place(GO_POWER_CHEST, cfg.chests);
    }

    void Populate(Map* map, Run& run, Party const& party)
    {
        Expedition const& d = expeditions.at(run.map);
        uint32 variant = (Day() + d.map) % cfg.variants + 1;
        auto hand = d.hand.find(uint8(variant));
        if (hand == d.hand.end()) hand = d.hand.find(0);
        std::vector<Position> spots;   // where powers can be found
        // in the nemesis lair the nemesis takes the boss's place
        bool lair = run.lair && cfg.nemesis && sObjectMgr->GetCreatureTemplate(cfg.nemesisEntry);
        if (hand != d.hand.end())
        {
            for (HandSpawn const& h : hand->second)
            {
                if (h.kind == K_CACHE) continue;
                bool boss = h.kind == K_BOSS;
                if (Creature* c = Place(map, run, boss && lair ? cfg.nemesisEntry : h.entry, h.pos, boss && lair ? K_NEMESIS : Kind(h.kind), party))
                {
                    if (boss) run.boss = c->GetGUID();
                    else spots.push_back(h.pos);
                }
            }
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
                if (Place(map, run, pts[idx].entry, pts[idx].pos, pts[idx].rank >= 1 ? K_ELITE : K_MOB, party)) spots.push_back(pts[idx].pos);
            }
            uint32 bossEntry = lair ? cfg.nemesisEntry : d.bossEntry ? d.bossEntry : pts[bossIdx].entry;
            if (Creature* c = Place(map, run, bossEntry, pts[bossIdx].pos, lair ? K_NEMESIS : K_BOSS, party)) run.boss = c->GetGUID();
        }
        if (lair) run.nemesis = run.boss;
        run.total = uint32(run.creatures.size());
        PlacePowerObjects(map, run, spots);
    }

    // the nemesis steps out next to a party member (ambush, lure or GM command)
    Creature* SpawnNemesis(Map* map, Run& run, Unit* near)
    {
        if (!cfg.nemesis || run.nemesis || run.done || run.failed || !near) return nullptr;
        Position pos = near->GetNearPosition(6.0f, float(rand_norm() * 2 * M_PI));
        Creature* c = Place(map, run, cfg.nemesisEntry, pos, K_NEMESIS, run.party);
        if (!c) return nullptr;
        run.nemesis = c->GetGUID();
        if (Unit* target = near->IsPlayer() ? near : nullptr) if (c->IsAIEnabled) c->AI()->AttackStart(target);
        return c;
    }

    // ------------------------------------------------------------------------------------------- slayer enemies
    std::string SlayerName(Player* player, Creature* c)
    {
        return Acore::StringFormat(Text(player, S_SLAYER_NAME, "Slayer {}"), c ? c->GetName() : std::string("?"));
    }

    // one normal enemy becomes the run's slayer: more health, more damage, bigger, and extra coins when it falls
    void PickSlayer(Map* map, Run& run)
    {
        if (!cfg.slayer || run.lair || run.tier < cfg.slayerFromTier || run.normals.empty() || !roll_chance_i(int32(cfg.slayerChance))) return;
        Creature* c = map->GetCreature(run.normals[urand(0, uint32(run.normals.size()) - 1)]);
        if (!c || !c->IsAlive()) return;
        uint32 hp = std::max<uint32>(1, uint32(c->GetCreateHealth() * cfg.slayerHealth));
        c->SetCreateHealth(hp);
        c->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(hp));
        for (WeaponAttackType a : { BASE_ATTACK, OFF_ATTACK, RANGED_ATTACK })
        {
            c->SetBaseWeaponDamage(a, MINDAMAGE, c->GetWeaponDamageRange(a, MINDAMAGE) * cfg.slayerDamage);
            c->SetBaseWeaponDamage(a, MAXDAMAGE, c->GetWeaponDamageRange(a, MAXDAMAGE) * cfg.slayerDamage);
        }
        c->UpdateAllStats();
        c->SetHealth(c->GetMaxHealth());
        c->SetObjectScale(c->GetObjectScale() * cfg.slayerScale);
        auto e = enemies.find(EnemyKey(run.instance, c->GetGUID()));
        if (e != enemies.end()) e->second.second *= cfg.slayerDamage;   // its spells too
        run.slayer = c->GetGUID();
    }

    // the slayer fell: every member on the map gets the extra coins (and ledger points)
    void SlayerDown(Map* map, Run& run, Creature* slayer)
    {
        run.slayer.Clear();
        map->DoForAllPlayers([&run, slayer](Player* p)
        {
            if (!run.members.count(p->GetGUID().GetCounter())) return;
            ChatHandler(p->GetSession()).PSendSysMessage(Text(p, S_SLAYER_DOWN, "|cffffcc00{} is slain!|r"), SlayerName(p, slayer));
            uint32 coins = cfg.slayerCoins;
            if (int32 bonus = CodeEffect(p, &run, E_COINS); bonus > 0) coins += coins * uint32(bonus) / 100;
            Give(p, cfg.coinItem, coins);
            AddLedger(p, cfg.slayerPoints);
        });
    }

    // ------------------------------------------------------------------------------------------- gravestones
    bool GravesHere(Run const& run)
    {
        if (!cfg.grave || !cfg.graveUses || !sObjectMgr->GetGameObjectTemplate(cfg.graveEntry)) return false;
        if (run.lair) return cfg.graveLair;
        return run.tier >= cfg.graveFromTier && (!cfg.graveToTier || run.tier <= cfg.graveToTier);
    }

    void RemoveGraveOf(Map* map, Run& run, ObjectGuid::LowType who)
    {
        for (auto it = run.graves.begin(); it != run.graves.end();)
        {
            if (it->second != who) { ++it; continue; }
            if (GameObject* go = map->GetGameObject(it->first)) go->Delete();
            it = run.graves.erase(it);
        }
    }

    // a member died: a gravestone where they fell, while they still have raises left this run
    void PlaceGrave(Player* dead, Run& run)
    {
        ObjectGuid::LowType me = dead->GetGUID().GetCounter();
        if (!GravesHere(run) || run.graveUses[me] >= cfg.graveUses) return;
        Map* map = dead->GetMap();
        RemoveGraveOf(map, run, me);
        GameObject* go = dead->SummonGameObject(cfg.graveEntry, dead->GetPositionX(), dead->GetPositionY(), dead->GetPositionZ(), dead->GetOrientation(),
            0, 0, 0, 0, cfg.graveSeconds, true, GO_SUMMON_TIMED_DESPAWN);
        if (!go) return;
        go->SetPhaseMask(cfg.phase, true);
        run.graves[go->GetGUID()] = me;
        std::string name = dead->GetName();
        map->DoForAllPlayers([&run, &name](Player* p)
        {
            if (run.members.count(p->GetGUID().GetCounter()))
                ChatHandler(p->GetSession()).PSendSysMessage(Text(p, S_GRAVE_PLACED, "{}'s gravestone marks where they fell. A party member can use it to raise them there."), name);
        });
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
        run.rich = IsRich(p.map) && !p.lair;
        run.lair = p.lair;
        Party party = parties.count(p.token) ? parties[p.token] : Party{ 1, player->GetLevel() };
        run.party = party;
        Populate(map, run, party);
        PickSlayer(map, run);
        // the nemesis may be lurking: from Nemesis.AmbushFromTier, with Nemesis.AmbushChance, after AmbushAtPercent of the enemies
        if (cfg.nemesis && !run.lair && run.tier >= cfg.ambushFrom && roll_chance_i(int32(cfg.ambushChance)))
            run.ambushAt = std::max<uint32>(1, run.total * cfg.ambushAt / 100);
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
                std::string tier = run.lair ? std::string(run.lair == 1 ? "?" : "??") : std::to_string(run.tier);
                ChatHandler(player->GetSession()).PSendSysMessage(Text(player, S_ENTER, "|cffffcc00Expedition:|r {} - tier {}{}. Defeat the expedition boss deep inside; {} enemies await."),
                    d.name, tier, run.affixes ? Acore::StringFormat(" ({})", AffixNames(run.affixes)) : std::string(), run.total);
                if (cfg.powers && !run.objects.empty())
                    ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_POWERS_HINT, "Strange Disturbances and chests inside give expedition powers; they last until you leave."));
                if (Creature* slayer = run.slayer ? map->GetCreature(run.slayer) : nullptr)
                    ChatHandler(player->GetSession()).PSendSysMessage(Text(player, S_SLAYER_APPEARS, "|cffff3333A {} stalks this expedition.|r It is tougher than the rest and carries extra {}."),
                        SlayerName(player, slayer), CoinName());
            }
            Refresh(player);
            Notify(player);
            return;
        }
        if (RunFor(player)) { EnterPhase(player); Refresh(player); Notify(player); return; }
        LeavePhase(player);
        Strip(player);
        Notify(player);
    }

    // ------------------------------------------------------------------------------------------- entering
    bool Enter(Player* player, uint32 mapId, uint32 tier, bool gm, uint8 lair = 0)
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
            Pending p; p.map = mapId; p.tier = tier; p.token = token; p.at = Now(); p.lair = lair;
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
        Strip(player);
        if (Run* run = RunFor(player))
        {
            RemoveGraveOf(player->GetMap(), *run, me);
            run->members.erase(me); run->powers.erase(me);
        }
        LeavePhase(player);
        Notify(player);
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
    // the cache's share of the newer features: coins, ledger points, relic / lure / bounty map finds, the lair's reward
    void FeatureRewards(Player* player, Run& run, uint32 keep)
    {
        if (cfg.coinItem && cfg.coinsPerTier)
        {
            uint32 coins = cfg.coinsPerTier * run.tier * keep / 100;
            if (int32 bonus = CodeEffect(player, &run, E_COINS); bonus > 0) coins += coins * uint32(bonus) / 100;
            Give(player, cfg.coinItem, coins);
        }
        uint32 points = cfg.ledgerPerTier * run.tier;
        if (run.lair)
        {
            auto lt = nemesisTiers.find(run.lair);
            if (lt != nemesisTiers.end())
            {
                Give(player, lt->second.item, lt->second.count);
                Give(player, cfg.coinItem, lt->second.coins);
                points += lt->second.points;
            }
            CharState& s = State(player);
            ++s.nemesisKills;
            s.lairBest = std::max<uint32>(s.lairBest, run.lair);
            SaveLedger(player->GetGUID().GetCounter(), s);
        }
        AddLedger(player, points);
        if (cfg.relics && roll_chance_i(int32(cfg.relicChance))) FindRelic(player);
        if (cfg.nemesis && cfg.lureItem && !run.lair && roll_chance_i(int32(cfg.lureChance))) Give(player, cfg.lureItem, 1);
        if (cfg.bounty && cfg.bountyItem && !player->HasItemCount(cfg.bountyItem, 1, true) && roll_chance_i(int32(cfg.bountyChance)))
            Give(player, cfg.bountyItem, 1);
    }

    // the nemesis fell in an ordinary run (ambush or lure): every member on the map gets the nemesis reward
    void NemesisDown(Map* map, Run& run, Creature* nemesis)
    {
        map->DoForAllPlayers([&run, nemesis](Player* p)
        {
            if (!run.members.count(p->GetGUID().GetCounter())) return;
            ChatHandler(p->GetSession()).PSendSysMessage(Text(p, S_NEMESIS_DOWN, "|cffffcc00Nemesis defeated:|r {} falls."), nemesis->GetName());
            Give(p, cfg.coinItem, cfg.nemesisCoins);
            Give(p, cfg.nemesisItem, cfg.nemesisCount);
            CharState& s = State(p);
            ++s.nemesisKills;
            SaveLedger(p->GetGUID().GetCounter(), s);
            AddLedger(p, cfg.nemesisPoints);
        });
    }

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
        FeatureRewards(player, run, keep);
        if (run.lair) return;   // the lair has its own two tiers; it does not open tiers of the dungeon it is in
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
        // someone followed a bounty map: the Hidden Trove stands next to the cache
        bool trove = false;
        if (cfg.bounty && !run.troveFor.empty())
        {
            Position p = boss->GetNearPosition(4.0f, boss->GetOrientation() + float(M_PI / 2));
            if (GameObject* go = boss->SummonGameObject(GO_TROVE, p.GetPositionX(), p.GetPositionY(), p.GetPositionZ(), boss->GetOrientation(), 0, 0, 0, 0, 3600, true, GO_SUMMON_TIMED_DESPAWN))
            {
                go->SetPhaseMask(cfg.phase, true);
                run.trove = go->GetGUID();
                trove = true;
            }
        }
        boss->GetMap()->DoForAllPlayers([&run, trove](Player* p)
        {
            if (!run.members.count(p->GetGUID().GetCounter())) return;
            ChatHandler(p->GetSession()).SendSysMessage(Text(p, S_BOSS_DOWN, "|cffffcc00Expedition complete!|r Open the Expedition Cache for your rewards."));
            if (trove) ChatHandler(p->GetSession()).SendSysMessage(Text(p, S_TROVE_APPEARS, "|cffffcc00A Hidden Trove appears!|r"));
        });
        NotifyRun(boss->GetMap(), run);
    }

    void Fail(Map* map, Run& run)
    {
        run.failed = true;
        map->DoForAllPlayers([&run](Player* p)
        {
            if (run.members.count(p->GetGUID().GetCounter()))
                ChatHandler(p->GetSession()).SendSysMessage(Text(p, S_FAILED, "|cffffcc00Expedition failed:|r you ran out of lives. Use .temucraft expedition leave to go back."));
        });
        Despawn(map, run);
        NotifyRun(map, run);
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

    void AddFeatureLines(Player* player);

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
        AddFeatureLines(player);
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

    // ------------------------------------------------------------------------------------------- relics, ledger, wares, lair (gossip)
    enum FeatureActions : uint32
    {
        A_RELICS = 5, A_LEDGER = 6, A_VENDOR = 7,
        A_LAIR_BASE = 4000,            // + hidden tier (1, 2)
        A_RELIC_VIEW_BASE = 10000,     // + relic id (below 10000)
        A_RELIC_EQUIP_BASE = 20000,
        A_RELIC_UPGRADE_BASE = 30000,
        A_RELIC_UNEQUIP_BASE = 40000,
        A_BUY_BASE = 50000             // + ware index
    };

    std::string NemesisName()
    {
        CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(cfg.nemesisEntry);
        return t ? t->Name : std::string("The Nemesis");
    }
    std::string HiddenTierName(uint32 n) { return n == 1 ? "?" : "??"; }
    uint32 BestTierAnywhere(Player* player)
    {
        if (QueryResult r = CharacterDatabase.Query("SELECT COALESCE(MAX(best_tier), 0) FROM mod_temucraft_expedition_progress WHERE owner = {}", player->GetGUID().GetCounter()))
            return r->Fetch()[0].Get<uint32>();
        return 0;
    }
    bool LairOpen() { return cfg.nemesis && !nemesisTiers.empty() && expeditions.count(cfg.lairMap) && sObjectMgr->GetCreatureTemplate(cfg.nemesisEntry); }
    bool LairUnlocked(Player* player, uint32 n)
    {
        if (cfg.unlockAll) return true;
        return n == 1 ? BestTierAnywhere(player) >= cfg.lairUnlock : State(player).lairBest >= n - 1;
    }

    void AddFeatureLines(Player* player)
    {
        if (LairOpen())
            for (auto const& [n, t] : nemesisTiers)
            {
                if (LairUnlocked(player, n))
                    AddGossipItemFor(player, GOSSIP_ICON_BATTLE, Acore::StringFormat(Text(player, S_LAIR_LINE, "{}'s Lair - tier {}"), NemesisName(), HiddenTierName(n)),
                        GOSSIP_SENDER_MAIN, A_LAIR_BASE + n);
                else if (n == 1)
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, Acore::StringFormat(Text(player, S_LAIR_LOCKED, "{}'s Lair - tier ?: finish tier {} of any expedition first"),
                        NemesisName(), cfg.lairUnlock), GOSSIP_SENDER_MAIN, A_NOOP);
                else
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, Acore::StringFormat(Text(player, S_LAIR_LOCKED2, "{}'s Lair - tier ??: defeat tier ? first"), NemesisName()),
                        GOSSIP_SENDER_MAIN, A_NOOP);
            }
        if (cfg.relics && !relicList.empty())
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, Text(player, S_RELICS, "Relics"), GOSSIP_SENDER_MAIN, A_RELICS);
        if (cfg.ledger && !ledgerRanks.empty())
        {
            CharState& s = State(player);
            AddGossipItemFor(player, GOSSIP_ICON_TRAINER, Acore::StringFormat(Text(player, S_LEDGER, "Wayfinder's Ledger (season {}): rank {}, {} points"), cfg.season, s.rank, s.points),
                GOSSIP_SENDER_MAIN, A_LEDGER);
        }
        if (cfg.coinItem && sObjectMgr->GetItemTemplate(cfg.coinItem))
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, Acore::StringFormat(Text(player, S_WALLET_COINS, "{}: {}"), CoinName(), CoinCount(player)), GOSSIP_SENDER_MAIN, A_NOOP);
    }

    std::string SlotName(Player* player, uint8 slot) { return slot == SLOT_UTILITY ? Text(player, S_SLOT_UTILITY, "utility") : Text(player, S_SLOT_COMBAT, "combat"); }
    std::string RelicName(Player* player, uint32 id) { Relic const* r = RelicById(id); return r ? r->name : std::string(Text(player, S_NONE, "none")); }

    void ShowRelics(Player* player, ObjectGuid source, uint32 textId)
    {
        ClearGossipMenuFor(player);
        CharState& s = State(player);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Acore::StringFormat(Text(player, S_RELIC_EQUIPPED, "Equipped: {} (combat), {} (utility)"),
            RelicName(player, s.combat), RelicName(player, s.utility)), GOSSIP_SENDER_MAIN, A_RELICS);
        uint32 shown = 0;
        for (uint8 slot : { SLOT_COMBAT, SLOT_UTILITY })
            for (auto const& [id, r] : relicList)
            {
                if (r.slot != slot || ++shown > 28) continue;
                auto owned = s.relics.find(id);
                if (owned != s.relics.end() && owned->second)
                {
                    bool on = s.combat == id || s.utility == id;
                    AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat(Text(player, S_RELIC_LINE, "[{}] {} - rank {}/{}: {}{}"), SlotName(player, slot), r.name,
                        owned->second, MAX_RELIC_RANK, EffectText(r.text, r.value[owned->second - 1]), on ? std::string(Text(player, S_RELIC_EQUIPPED_MARK, " (equipped)")) : std::string()),
                        GOSSIP_SENDER_MAIN, A_RELIC_VIEW_BASE + id);
                }
                else
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, Acore::StringFormat(Text(player, S_RELIC_LOCKED, "[{}] {} - not found yet: {}"), SlotName(player, slot), r.name,
                        EffectText(r.text, r.value[0])), GOSSIP_SENDER_MAIN, A_RELIC_VIEW_BASE + id);
            }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_BACK, "Back"), GOSSIP_SENDER_MAIN, A_MAIN);
        SendGossipMenuFor(player, textId, source);
    }

    void ShowRelic(Player* player, ObjectGuid source, uint32 textId, uint32 id)
    {
        Relic const* r = RelicById(id);
        if (!r) { ShowRelics(player, source, textId); return; }
        ClearGossipMenuFor(player);
        CharState& s = State(player);
        auto owned = s.relics.find(id);
        uint8 rank = owned == s.relics.end() ? 0 : owned->second;
        bool on = s.combat == id || s.utility == id;
        if (rank)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat(Text(player, S_RELIC_LINE, "[{}] {} - rank {}/{}: {}{}"), SlotName(player, r->slot), r->name,
                rank, MAX_RELIC_RANK, EffectText(r->text, r->value[rank - 1]), on ? std::string(Text(player, S_RELIC_EQUIPPED_MARK, " (equipped)")) : std::string()),
                GOSSIP_SENDER_MAIN, A_RELIC_VIEW_BASE + id);
        if (rank && !on)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat(Text(player, S_RELIC_EQUIP, "Equip {}"), r->name), GOSSIP_SENDER_MAIN, A_RELIC_EQUIP_BASE + id);
        if (on)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat(Text(player, S_RELIC_UNEQUIP, "Take off {}"), r->name), GOSSIP_SENDER_MAIN, A_RELIC_UNEQUIP_BASE + id);
        if (rank >= MAX_RELIC_RANK)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_RELIC_MAXED, "This relic is at its highest rank."), GOSSIP_SENDER_MAIN, A_RELIC_VIEW_BASE + id);
        else if (r->cost[rank] || rank)
        {
            uint32 cost = r->cost[rank];
            std::string line = rank
                ? Acore::StringFormat(Text(player, S_RELIC_UPGRADE, "Upgrade to rank {} ({}) for {} {}"), rank + 1, EffectText(r->text, r->value[rank]), cost, CoinName())
                : Acore::StringFormat(Text(player, S_RELIC_UNLOCK, "Unlock rank 1 ({}) for {} {}"), EffectText(r->text, r->value[0]), cost, CoinName());
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, line, GOSSIP_SENDER_MAIN, A_RELIC_UPGRADE_BASE + id, line + "?", 0, false);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_BACK, "Back"), GOSSIP_SENDER_MAIN, A_RELICS);
        SendGossipMenuFor(player, textId, source);
    }

    // relic actions; true when the action was one of them
    bool RelicAction(Player* player, ObjectGuid source, uint32 textId, uint32 action)
    {
        if (action == A_RELICS) { ShowRelics(player, source, textId); return true; }
        if (action < A_RELIC_VIEW_BASE || action >= A_BUY_BASE) return false;
        uint32 base = action - action % 10000, id = action % 10000;
        Relic const* r = RelicById(id);
        if (!r || !cfg.relics) { ShowRelics(player, source, textId); return true; }
        ObjectGuid::LowType me = player->GetGUID().GetCounter();
        CharState& s = State(player);
        uint8 rank = s.relics.count(id) ? s.relics[id] : 0;
        uint32& slot = r->slot == SLOT_UTILITY ? s.utility : s.combat;
        ChatHandler h(player->GetSession());
        if (base == A_RELIC_EQUIP_BASE && rank)
        {
            slot = id;
            SaveChar(me, s);
            h.PSendSysMessage(Text(player, S_RELIC_NOW_EQUIPPED, "{} equipped in the {} slot."), r->name, SlotName(player, r->slot));
            Refresh(player);
        }
        else if (base == A_RELIC_UNEQUIP_BASE && slot == id)
        {
            slot = 0;
            SaveChar(me, s);
            h.PSendSysMessage(Text(player, S_RELIC_NOW_UNEQUIPPED, "{} taken off."), r->name);
            Refresh(player);
        }
        else if (base == A_RELIC_UPGRADE_BASE && rank < MAX_RELIC_RANK && (rank || r->cost[0]))
        {
            if (Pay(player, r->cost[rank]))
            {
                s.relics[id] = ++rank;
                SaveRelic(me, id, rank);
                if (rank == 1 && !slot) { slot = id; SaveChar(me, s); }   // a first relic in an empty slot goes on at once
                h.PSendSysMessage(Text(player, S_RELIC_UPGRADED, "{} is now rank {}."), r->name, rank);
                Refresh(player);
            }
        }
        ShowRelic(player, source, textId, id);
        return true;
    }

    std::string LedgerRewardText(Player* player, LedgerRank const& r)
    {
        std::string out;
        auto add = [&out](std::string const& part) { if (!out.empty()) out += ", "; out += part; };
        if (r.item && r.count) add(Acore::StringFormat("{} x{}", ItemName(r.item), r.count));
        if (r.coins) add(Acore::StringFormat("{} {}", r.coins, CoinName()));
        if (r.gold) add(Acore::StringFormat("{} gold", r.gold / 10000));
        return out.empty() ? std::string(Text(player, S_LEDGER_NOTHING, "nothing")) : out;
    }

    void ShowLedger(Player* player, ObjectGuid source, uint32 textId)
    {
        ClearGossipMenuFor(player);
        CharState& s = State(player);
        auto next = ledgerRanks.upper_bound(s.rank);
        std::string tail = next == ledgerRanks.end() ? std::string(Text(player, S_LEDGER_DONE, " - every rank reached"))
            : Acore::StringFormat(Text(player, S_LEDGER_NEXT, " - next rank at {} points"), next->second.points);
        AddGossipItemFor(player, GOSSIP_ICON_TRAINER, Acore::StringFormat(Text(player, S_LEDGER, "Wayfinder's Ledger (season {}): rank {}, {} points"), cfg.season, s.rank, s.points) + tail,
            GOSSIP_SENDER_MAIN, A_LEDGER);
        uint32 shown = 0;
        for (auto const& [n, r] : ledgerRanks)
        {
            if (n + 2 < s.rank || ++shown > 20) continue;
            AddGossipItemFor(player, n <= s.rank ? GOSSIP_ICON_CHAT : GOSSIP_ICON_MONEY_BAG, Acore::StringFormat(Text(player, S_LEDGER_RANK, "Rank {} at {} points: {}{}"), n, r.points,
                LedgerRewardText(player, r), n <= s.rank ? " (done)" : ""), GOSSIP_SENDER_MAIN, A_LEDGER);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_BACK, "Back"), GOSSIP_SENDER_MAIN, A_MAIN);
        SendGossipMenuFor(player, textId, source);
    }

    void ShowVendor(Player* player, ObjectGuid source, uint32 textId)
    {
        ClearGossipMenuFor(player);
        if (cfg.coinItem && sObjectMgr->GetItemTemplate(cfg.coinItem))
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, Acore::StringFormat(Text(player, S_WALLET_COINS, "{}: {}"), CoinName(), CoinCount(player)), GOSSIP_SENDER_MAIN, A_VENDOR);
        for (size_t i = 0; i < vendorItems.size() && i < 28; ++i)
        {
            VendorItem const& v = vendorItems[i];
            std::string line = Acore::StringFormat(Text(player, S_VENDOR_LINE, "{} x{} - {} {}"), ItemName(v.item), v.count, v.cost, CoinName());
            if (v.rank) line += Acore::StringFormat(Text(player, S_VENDOR_NEED_RANK, " (Ledger rank {})"), v.rank);
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, line, GOSSIP_SENDER_MAIN, A_BUY_BASE + uint32(i),
                Acore::StringFormat(Text(player, S_VENDOR_CONFIRM, "Buy {} x{} for {} {}?"), ItemName(v.item), v.count, v.cost, CoinName()), 0, false);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_BACK, "Back"), GOSSIP_SENDER_MAIN, A_MAIN);
        SendGossipMenuFor(player, textId, source);
    }

    void Buy(Player* player, uint32 index)
    {
        if (!cfg.vendor || index >= vendorItems.size()) return;
        VendorItem const& v = vendorItems[index];
        ChatHandler h(player->GetSession());
        if (v.rank && State(player).rank < v.rank)
        {
            h.PSendSysMessage(Text(player, S_VENDOR_NEED_RANK, " (Ledger rank {})"), v.rank);
            return;
        }
        ItemPosCountVec dest;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, v.item, v.count) != EQUIP_ERR_OK)
        {
            player->SendEquipError(EQUIP_ERR_INVENTORY_FULL, nullptr, nullptr, v.item);
            return;
        }
        if (!Pay(player, v.cost)) return;
        Give(player, v.item, v.count, false);
        h.PSendSysMessage(Text(player, S_VENDOR_BOUGHT, "Bought {} x{}."), ItemName(v.item), v.count);
    }

    class npc_temucraft_expedition_keeper : public CreatureScript
    {
    public:
        npc_temucraft_expedition_keeper() : CreatureScript("npc_temucraft_expedition_keeper") { }

        bool OnGossipHello(Player* player, Creature* creature) override
        {
            ExpeditionsGuardT expeditionsGuard;
            if (!cfg.enable) { ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_OFF, "Expeditions are closed.")); return true; }
            ShowMain(player, creature);
            return true;
        }

        bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
        {
            ExpeditionsGuardT expeditionsGuard;
            if (!cfg.enable) { CloseGossipMenuFor(player); return true; }
            if (action == A_MAIN || action == A_NOOP) { ShowMain(player, creature); return true; }
            if (action == A_RICH) { ShowRich(player, creature); return true; }
            if (action == A_LEDGER) { ShowLedger(player, creature->GetGUID(), TEXT_KEEPER); return true; }
            if (RelicAction(player, creature->GetGUID(), TEXT_KEEPER, action)) { Notify(player); return true; }
            if (action > A_LAIR_BASE && action <= A_LAIR_BASE + 2)
            {
                uint32 n = action - A_LAIR_BASE;
                CloseGossipMenuFor(player);
                auto t = nemesisTiers.find(n);
                if (LairOpen() && t != nemesisTiers.end() && LairUnlocked(player, n) && tiers.count(t->second.baseTier))
                    Enter(player, cfg.lairMap, t->second.baseTier, false, uint8(n));
                return true;
            }
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
            ExpeditionsGuardT expeditionsGuard;
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
            ExpeditionsGuardT expeditionsGuard;
            CloseGossipMenuFor(player);
            if (action == 1) { if (Run* run = RunFor(player)) Reward(player, *run); }
            else if (action == 2) Leave(player);
            return true;
        }
    };

    // the Wayfinder Quartermaster: wares for Wayfinder Coins, relics and the ledger
    class npc_temucraft_expedition_quartermaster : public CreatureScript
    {
    public:
        npc_temucraft_expedition_quartermaster() : CreatureScript("npc_temucraft_expedition_quartermaster") { }

        static void Main(Player* player, Creature* creature)
        {
            ClearGossipMenuFor(player);
            if (cfg.vendor && !vendorItems.empty())
                AddGossipItemFor(player, GOSSIP_ICON_VENDOR, Text(player, S_VENDOR, "Wayfinder Quartermaster wares"), GOSSIP_SENDER_MAIN, A_VENDOR);
            if (cfg.relics && !relicList.empty())
                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, Text(player, S_RELICS, "Relics"), GOSSIP_SENDER_MAIN, A_RELICS);
            if (cfg.ledger && !ledgerRanks.empty())
            {
                CharState& s = State(player);
                AddGossipItemFor(player, GOSSIP_ICON_TRAINER, Acore::StringFormat(Text(player, S_LEDGER, "Wayfinder's Ledger (season {}): rank {}, {} points"), cfg.season, s.rank, s.points),
                    GOSSIP_SENDER_MAIN, A_LEDGER);
            }
            if (cfg.coinItem && sObjectMgr->GetItemTemplate(cfg.coinItem))
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, Acore::StringFormat(Text(player, S_WALLET_COINS, "{}: {}"), CoinName(), CoinCount(player)), GOSSIP_SENDER_MAIN, A_MAIN);
            SendGossipMenuFor(player, TEXT_QUARTERMASTER, creature->GetGUID());
        }

        bool OnGossipHello(Player* player, Creature* creature) override
        {
            ExpeditionsGuardT expeditionsGuard;
            if (!cfg.enable) { ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_OFF, "Expeditions are closed.")); return true; }
            Main(player, creature);
            return true;
        }

        bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
        {
            ExpeditionsGuardT expeditionsGuard;
            if (!cfg.enable) { CloseGossipMenuFor(player); return true; }
            if (action == A_VENDOR) { ShowVendor(player, creature->GetGUID(), TEXT_QUARTERMASTER); return true; }
            if (action == A_LEDGER) { ShowLedger(player, creature->GetGUID(), TEXT_QUARTERMASTER); return true; }
            if (action >= A_BUY_BASE && action < A_EXPEDITION_BASE)
            {
                Buy(player, action - A_BUY_BASE);
                Notify(player);
                ShowVendor(player, creature->GetGUID(), TEXT_QUARTERMASTER);
                return true;
            }
            if (RelicAction(player, creature->GetGUID(), TEXT_QUARTERMASTER, action)) { Notify(player); return true; }
            Main(player, creature);
            return true;
        }
    };

    // a Strange Disturbance: pick one of a few expedition powers, once per player
    class go_temucraft_expedition_disturbance : public GameObjectScript
    {
    public:
        go_temucraft_expedition_disturbance() : GameObjectScript("go_temucraft_expedition_disturbance") { }

        static std::vector<uint32> Offer(Run const& run, Player* player, GameObject* go)
        {
            return PowerChoices(run, player->GetGUID().GetCounter(), go->GetGUID().GetCounter() * 7919u + player->GetGUID().GetCounter(), cfg.choices);
        }

        bool OnGossipHello(Player* player, GameObject* go) override
        {
            ExpeditionsGuardT expeditionsGuard;
            ClearGossipMenuFor(player);
            Run* run = RunFor(player);
            if (!run || !cfg.powers) { CloseGossipMenuFor(player); return true; }
            if (run->used[go->GetGUID()].count(player->GetGUID().GetCounter()))
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, Text(player, S_DISTURBANCE_USED, "The disturbance has gone quiet for you."), GOSSIP_SENDER_MAIN, 0);
            else
                for (uint32 id : Offer(*run, player, go))
                    if (Power const* p = PowerById(id))
                        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat(Text(player, S_DISTURBANCE_PICK, "Take: {} - {}"), p->name, EffectText(p->text, p->value)),
                            GOSSIP_SENDER_MAIN, id);
            SendGossipMenuFor(player, TEXT_DISTURBANCE, go->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, GameObject* go, uint32 /*sender*/, uint32 action) override
        {
            ExpeditionsGuardT expeditionsGuard;
            CloseGossipMenuFor(player);
            Run* run = RunFor(player);
            if (!run || !cfg.powers || !action) return true;
            std::set<ObjectGuid::LowType>& used = run->used[go->GetGUID()];
            if (used.count(player->GetGUID().GetCounter())) return true;
            std::vector<uint32> offer = Offer(*run, player, go);
            if (std::find(offer.begin(), offer.end(), action) == offer.end()) return true;
            used.insert(player->GetGUID().GetCounter());
            GainPower(player, *run, action);
            return true;
        }
    };

    // a chest inside: a random expedition power and a few coins, once per player
    class go_temucraft_expedition_power_chest : public GameObjectScript
    {
    public:
        go_temucraft_expedition_power_chest() : GameObjectScript("go_temucraft_expedition_power_chest") { }

        bool OnGossipHello(Player* player, GameObject* go) override
        {
            ExpeditionsGuardT expeditionsGuard;
            ClearGossipMenuFor(player);
            Run* run = RunFor(player);
            if (!run || !cfg.powers) { CloseGossipMenuFor(player); return true; }
            if (!run->used[go->GetGUID()].count(player->GetGUID().GetCounter()))
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, Text(player, S_POWER_CHEST_OPEN, "Open the chest"), GOSSIP_SENDER_MAIN, 1);
            SendGossipMenuFor(player, TEXT_POWER_CHEST, go->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, GameObject* go, uint32 /*sender*/, uint32 action) override
        {
            ExpeditionsGuardT expeditionsGuard;
            CloseGossipMenuFor(player);
            Run* run = RunFor(player);
            if (!run || !cfg.powers || action != 1) return true;
            std::set<ObjectGuid::LowType>& used = run->used[go->GetGUID()];
            if (!used.insert(player->GetGUID().GetCounter()).second) return true;
            std::vector<uint32> pick = PowerChoices(*run, player->GetGUID().GetCounter(), urand(1, 0x7FFFFFFF), 1);
            if (!pick.empty()) GainPower(player, *run, pick[0]);
            Give(player, cfg.coinItem, cfg.chestCoins);
            Notify(player);
            return true;
        }
    };

    // the Hidden Trove of a bounty map: only for the players who used one in this run
    class go_temucraft_expedition_trove : public GameObjectScript
    {
    public:
        go_temucraft_expedition_trove() : GameObjectScript("go_temucraft_expedition_trove") { }

        bool OnGossipHello(Player* player, GameObject* go) override
        {
            ExpeditionsGuardT expeditionsGuard;
            ClearGossipMenuFor(player);
            Run* run = RunFor(player);
            ObjectGuid::LowType me = player->GetGUID().GetCounter();
            if (!run || !cfg.bounty) { CloseGossipMenuFor(player); return true; }
            if (!run->troveFor.count(me)) ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_TROVE_NOT_YOURS, "This trove was found with someone else's map."));
            else if (!run->troveClaimed.count(me))
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, Text(player, S_TROVE_OPEN, "Open the Hidden Trove"), GOSSIP_SENDER_MAIN, 1);
            SendGossipMenuFor(player, TEXT_TROVE, go->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, GameObject* /*go*/, uint32 /*sender*/, uint32 action) override
        {
            ExpeditionsGuardT expeditionsGuard;
            CloseGossipMenuFor(player);
            Run* run = RunFor(player);
            ObjectGuid::LowType me = player->GetGUID().GetCounter();
            if (!run || !cfg.bounty || action != 1 || !run->troveFor.count(me) || !run->troveClaimed.insert(me).second) return true;
            if (cfg.troveGold)
            {
                player->ModifyMoney(cfg.troveGold);
                ChatHandler(player->GetSession()).PSendSysMessage(Text(player, S_REWARD_GOLD, "  Cache: {} gold."), cfg.troveGold / 10000);
            }
            Give(player, cfg.coinItem, cfg.troveCoins);
            Give(player, cfg.troveItem, cfg.troveCount);
            if (roll_chance_i(int32(cfg.troveRelicChance))) FindRelic(player);
            return true;
        }
    };

    // a gravestone: a party member raises the player who fell here, at this spot
    class go_temucraft_expedition_gravestone : public GameObjectScript
    {
    public:
        go_temucraft_expedition_gravestone() : GameObjectScript("go_temucraft_expedition_gravestone") { }

        static Player* Fallen(Run const& run, GameObject* go)
        {
            auto it = run.graves.find(go->GetGUID());
            return it == run.graves.end() ? nullptr : ObjectAccessor::FindPlayerByLowGUID(it->second);
        }

        bool OnGossipHello(Player* player, GameObject* go) override
        {
            ExpeditionsGuardT expeditionsGuard;
            ClearGossipMenuFor(player);
            Run* run = RunFor(player);
            if (!run || !cfg.grave || !run->graves.count(go->GetGUID())) { CloseGossipMenuFor(player); return true; }
            Player* dead = Fallen(*run, go);
            if (run->graves[go->GetGUID()] == player->GetGUID().GetCounter())
                ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_GRAVE_OWN, "Your party has to use your gravestone to raise you."));
            else if (dead && !dead->IsAlive() && player->IsAlive())
                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat(Text(player, S_GRAVE_RAISE, "Raise {} here"), dead->GetName()), GOSSIP_SENDER_MAIN, 1);
            else
                ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_GRAVE_NOBODY, "There is nobody to raise here."));
            SendGossipMenuFor(player, cfg.graveEntry, go->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, GameObject* go, uint32 /*sender*/, uint32 action) override
        {
            ExpeditionsGuardT expeditionsGuard;
            CloseGossipMenuFor(player);
            Run* run = RunFor(player);
            if (!run || !cfg.grave || action != 1 || !player->IsAlive()) return true;
            auto it = run->graves.find(go->GetGUID());
            if (it == run->graves.end() || it->second == player->GetGUID().GetCounter()) return true;
            Player* dead = Fallen(*run, go);
            if (!dead || dead->IsAlive()) return true;
            if (dead->GetMap() != player->GetMap() || !run->members.count(dead->GetGUID().GetCounter()))
            {
                ChatHandler(player->GetSession()).PSendSysMessage(Text(player, S_GRAVE_AWAY, "{} has to be inside the dungeon to be raised here (run back first)."), dead->GetName());
                return true;
            }
            ++run->graveUses[it->second];
            run->graves.erase(it);
            Position spot = go->GetPosition();
            go->Delete();
            dead->ResurrectPlayer(cfg.graveRestore);
            dead->SpawnCorpseBones();
            dead->NearTeleportTo(spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), spot.GetOrientation());
            std::string raiser = player->GetName(), raised = dead->GetName();
            player->GetMap()->DoForAllPlayers([run, &raiser, &raised](Player* p)
            {
                if (run->members.count(p->GetGUID().GetCounter()))
                    ChatHandler(p->GetSession()).PSendSysMessage(Text(p, S_GRAVE_RAISED, "{} raises {} at the gravestone."), raiser, raised);
            });
            return true;
        }
    };

    // the item's use spell is a dummy; the script does the work, then tells the client the cast is over
    void EndItemUse(Player* player, Item* item)
    {
        if (!item) return;
        ItemTemplate const* proto = item->GetTemplate();
        if (proto && proto->Spells[0].SpellId)
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(proto->Spells[0].SpellId))
                Spell::SendCastResult(player, info, 0, SPELL_FAILED_DONT_REPORT);
    }

    // the Nemesis Lure: call the nemesis into the current run, once per run
    class item_temucraft_expedition_lure : public ItemScript
    {
    public:
        item_temucraft_expedition_lure() : ItemScript("item_temucraft_expedition_lure") { }

        bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
        {
            ExpeditionsGuardT expeditionsGuard;
            Run* run = RunFor(player);
            if (!cfg.nemesis || !run || run->lair || run->done || run->failed || run->lureUsed || run->nemesis)
            {
                ChatHandler(player->GetSession()).SendSysMessage(Text(player, S_LURE_NO, "The lure only works inside an expedition that is still running, once per run, and not in the nemesis lair."));
                EndItemUse(player, item);
                return true;
            }
            EndItemUse(player, item);
            Creature* n = SpawnNemesis(player->GetMap(), *run, player);
            if (!n) return true;
            run->lureUsed = true;
            player->DestroyItemCount(item->GetEntry(), 1, true);
            Run const& r = *run;
            std::string name = n->GetName();
            player->GetMap()->DoForAllPlayers([&r, &name](Player* p)
            {
                if (r.members.count(p->GetGUID().GetCounter()))
                    ChatHandler(p->GetSession()).PSendSysMessage(Text(p, S_NEMESIS_SUMMONED, "|cffff3333The lure sounds. {} answers the call!|r"), name);
            });
            return true;
        }
    };

    // the bounty map: once a week, makes a Hidden Trove appear when this run's boss falls
    class item_temucraft_expedition_bounty : public ItemScript
    {
    public:
        item_temucraft_expedition_bounty() : ItemScript("item_temucraft_expedition_bounty") { }

        bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
        {
            ExpeditionsGuardT expeditionsGuard;
            ChatHandler h(player->GetSession());
            EndItemUse(player, item);
            Run* run = RunFor(player);
            ObjectGuid::LowType me = player->GetGUID().GetCounter();
            if (!cfg.bounty || !run || run->done || run->failed || run->troveFor.count(me))
            {
                h.SendSysMessage(Text(player, S_BOUNTY_NO, "Use the bounty map inside an expedition that is still running."));
                return true;
            }
            CharState& s = State(player);
            if (s.bountyWeek == Week()) { h.SendSysMessage(Text(player, S_BOUNTY_WEEK, "You already followed a bounty map this week.")); return true; }
            s.bountyWeek = Week();
            SaveChar(me, s);
            run->troveFor.insert(me);
            player->DestroyItemCount(item->GetEntry(), 1, true);
            h.SendSysMessage(Text(player, S_BOUNTY_USED, "The map shows a Hidden Trove in this expedition. It opens when the expedition boss falls."));
            return true;
        }
    };

    // the nemesis: melee, a few spells on timers (Nemesis.Spells) and an enrage at low health
    class npc_temucraft_expedition_nemesis : public CreatureScript
    {
    public:
        npc_temucraft_expedition_nemesis() : CreatureScript("npc_temucraft_expedition_nemesis") { }

        struct npc_temucraft_expedition_nemesisAI : public ScriptedAI
        {
            npc_temucraft_expedition_nemesisAI(Creature* creature) : ScriptedAI(creature) { }

            void Reset() override { ExpeditionsGuardT expeditionsGuard; events.Reset(); enraged = false; }

            void JustEngagedWith(Unit* /*who*/) override
            {
                ExpeditionsGuardT expeditionsGuard;
                events.Reset();
                for (size_t i = 0; i < cfg.nemesisSpells.size(); ++i)
                    events.ScheduleEvent(uint32(i + 1), Milliseconds(cfg.nemesisSpells[i].second * 500), Milliseconds(cfg.nemesisSpells[i].second * 1000));
            }

            void UpdateAI(uint32 diff) override
            {
                ExpeditionsGuardT expeditionsGuard;
                if (!UpdateVictim()) return;
                events.Update(diff);
                if (!enraged && cfg.enrageSpell && cfg.enrageAt && me->HealthBelowPct(int32(cfg.enrageAt)))
                {
                    enraged = true;
                    DoCastSelf(cfg.enrageSpell, true);
                }
                if (me->HasUnitState(UNIT_STATE_CASTING)) return;
                while (uint32 e = events.ExecuteEvent())
                {
                    if (e > cfg.nemesisSpells.size()) continue;
                    auto const& [spell, every] = cfg.nemesisSpells[e - 1];
                    DoCastVictim(spell);
                    events.ScheduleEvent(e, Milliseconds(every * 1000));
                    if (me->HasUnitState(UNIT_STATE_CASTING)) return;
                }
                DoMeleeAttackIfReady();
            }

        private:
            EventMap events;
            bool enraged = false;
        };

        CreatureAI* GetAI(Creature* creature) const override { ExpeditionsGuardT expeditionsGuard; return new npc_temucraft_expedition_nemesisAI(creature); }
    };

    // ------------------------------------------------------------------------------------------- hooks
    class temucraft_expeditions_player : public PlayerScript
    {
    public:
        temucraft_expeditions_player() : PlayerScript("temucraft_expeditions_player",
            { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_ON_PLAYER_JUST_DIED, PLAYERHOOK_ON_DELETE,
              PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT }) { }

        using PlayerScript::OnPlayerCanUseChat;
        // the TemucraftExpeditions window whispering itself; swallowed so nothing shows in chat
        bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* receiver) override
        {
            ExpeditionsGuardT expeditionsGuard;
            std::string body;
            if (!TemucraftAddonParse(player, type, language, msg, receiver, ADDON_PREFIX, body)) return true;
            if (body == "HELLO" || body == "GET") Notify(player);
            return false;
        }

        void OnPlayerLogin(Player* player) override { ExpeditionsGuardT expeditionsGuard; if (cfg.enable) Arrive(player); }
        void OnPlayerMapChanged(Player* player) override { ExpeditionsGuardT expeditionsGuard; if (cfg.enable) Arrive(player); }
        void OnPlayerLogout(Player* player) override
        {
            ExpeditionsGuardT expeditionsGuard;
            // the phase isn't saved; at login inside the instance Arrive() puts a member back in the expedition phase
            ObjectGuid::LowType me = player->GetGUID().GetCounter();
            pending.erase(me); viewing.erase(me); previousPhase.erase(me);
            absorbAt.erase(AbsorbKeyOf(player));
            std::lock_guard<std::mutex> guard(charsLock);
            chars.erase(me);
        }
        void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
        {
            ExpeditionsGuardT expeditionsGuard;
            CharacterDatabase.Execute("DELETE FROM mod_temucraft_expedition_progress WHERE owner = {}", guid.GetCounter());
            CharacterDatabase.Execute("DELETE FROM mod_temucraft_expedition_wallet WHERE owner = {}", guid.GetCounter());
            CharacterDatabase.Execute("DELETE FROM mod_temucraft_expedition_relic WHERE owner = {}", guid.GetCounter());
            CharacterDatabase.Execute("DELETE FROM mod_temucraft_expedition_character WHERE owner = {}", guid.GetCounter());
            CharacterDatabase.Execute("DELETE FROM mod_temucraft_expedition_ledger WHERE owner = {}", guid.GetCounter());
        }
        void OnPlayerJustDied(Player* player) override
        {
            ExpeditionsGuardT expeditionsGuard;
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
            PlaceGrave(player, *run);
            NotifyRun(player->GetMap(), *run);
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
            auto it = enemies.find(EnemyOf(attacker));
            return it == enemies.end() ? 1.0f : it->second.second;
        }
        void ModifySpellDamageTaken(Unit* /*target*/, Unit* attacker, int32& damage, SpellInfo const* /*spellInfo*/) override
        {
            ExpeditionsGuardT expeditionsGuard;
            float f = Factor(attacker);
            if (f != 1.0f && damage > 0) damage = int32(damage * f);
        }
        void ModifyPeriodicDamageAurasTick(Unit* /*target*/, Unit* attacker, uint32& damage, SpellInfo const* /*spellInfo*/) override
        {
            ExpeditionsGuardT expeditionsGuard;
            float f = Factor(attacker);
            if (f != 1.0f) damage = uint32(damage * f);
        }
        void OnUnitDeath(Unit* unit, Unit* killer) override
        {
            ExpeditionsGuardT expeditionsGuard;
            if (enemies.empty() || !unit || !unit->IsCreature()) return;
            auto it = enemies.find(EnemyOf(unit));
            if (it == enemies.end()) return;
            uint32 instance = it->second.first;
            enemies.erase(it);
            auto r = runs.find(instance);
            if (r == runs.end() || r->second.failed) return;
            Run& run = r->second;
            if (unit->GetGUID() == run.boss && !run.done) { ++run.kills; Complete(unit->ToCreature(), run); return; }
            if (unit->GetGUID() == run.nemesis) { NemesisDown(unit->GetMap(), run, unit->ToCreature()); return; }
            if (unit->GetGUID() == run.slayer) SlayerDown(unit->GetMap(), run, unit->ToCreature());
            ++run.kills;
            NotifyRun(unit->GetMap(), run);
            // the nemesis ambush, once enough enemies are down
            if (run.ambushAt && !run.nemesis && !run.done && run.kills >= run.ambushAt)
            {
                run.ambushAt = 0;
                Unit* near = killer && killer->GetCharmerOrOwnerPlayerOrPlayerItself() ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
                if (!near)
                    unit->GetMap()->DoForAllPlayers([&run, &near](Player* p) { if (!near && p->IsAlive() && run.members.count(p->GetGUID().GetCounter())) near = p; });
                if (Creature* n = SpawnNemesis(unit->GetMap(), run, near))
                    unit->GetMap()->DoForAllPlayers([&run, n](Player* p)
                    {
                        if (run.members.count(p->GetGUID().GetCounter()))
                            ChatHandler(p->GetSession()).PSendSysMessage(Text(p, S_NEMESIS_AMBUSH, "|cffff3333{} ambushes you!|r Defeat it for a nemesis reward, or press on."), n->GetName());
                    });
            }
        }
    };

    class temucraft_expeditions_world : public WorldScript
    {
    public:
        temucraft_expeditions_world() : WorldScript("temucraft_expeditions_world", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE }) { }
        void OnAfterConfigLoad(bool /*reload*/) override { ExpeditionsGuardT expeditionsGuard; LoadSettings(); }
        void OnStartup() override { ExpeditionsGuardT expeditionsGuard; LoadData(); }
        void OnUpdate(uint32 diff) override
        {
            ExpeditionsGuardT expeditionsGuard;
            // relic and power auras: put back after a death, follow the companion, renew used-up shields
            refresh += diff;
            if (refresh >= 2000 && !auraSpells.empty())
            {
                refresh = 0;
                for (auto& [instance, run] : runs)
                    for (ObjectGuid::LowType m : run.members)
                        if (Player* p = ObjectAccessor::FindPlayerByLowGUID(m))
                            if (p->IsInWorld() && RunFor(p)) Refresh(p);
            }
            timer += diff;
            if (timer < 60000) return;
            timer = 0;
            for (auto it = absorbAt.begin(); it != absorbAt.end();)
                it = Now() - it->second > 3600 ? absorbAt.erase(it) : std::next(it);
            // forget runs whose instance is gone, and teleports that never arrived
            for (auto it = runs.begin(); it != runs.end();)
            {
                if (!sMapMgr->FindMap(it->second.map, it->first))
                {
                    for (ObjectGuid const& g : it->second.creatures) enemies.erase(EnemyKey(it->first, g));

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
        uint32 timer = 0, refresh = 0;
    };

    class temucraft_expeditions_commands : public CommandScript
    {
    public:
        temucraft_expeditions_commands() : CommandScript("temucraft_expeditions_commands") { }

        ChatCommandTable GetCommands() const override
        {
            ExpeditionsGuardT expeditionsGuard;
            static ChatCommandTable sub =
            {
                { "leave",  HandleLeave,  100550,        Console::No },
                { "status", HandleStatus, 100551,        Console::No },
                { "start",  HandleStart,  100501 /* SEC_GAMEMASTER -> RBAC */,    Console::No },
                { "list",   HandleList,   100502 /* SEC_GAMEMASTER -> RBAC */,    Console::Yes },
                { "reload", HandleReload, 100503 /* SEC_ADMINISTRATOR -> RBAC */, Console::Yes },
                { "here",   HandleHere,   100504 /* SEC_GAMEMASTER -> RBAC */,    Console::No },
                { "spot",   HandleSpot,   100505 /* SEC_GAMEMASTER -> RBAC */,    Console::No },
                { "unspot", HandleUnspot, 100506 /* SEC_GAMEMASTER -> RBAC */,    Console::No },
                { "spots",  HandleSpots,  100507 /* SEC_GAMEMASTER -> RBAC */,    Console::No },
                { "powers", HandlePowers, 100552,        Console::No },
                { "relics", HandleRelics, 100553,        Console::No },
                { "ledger", HandleLedger, 100554,        Console::No },
                { "nemesis", HandleNemesis, 100508 /* SEC_GAMEMASTER -> RBAC */, Console::No },
            };
            static ChatCommandTable root = { { "temucraft expedition", sub } };
            return root;
        }

        static bool HandleLeave(ChatHandler* handler)
        {
            ExpeditionsGuardT expeditionsGuard;
            Player* p = handler->GetPlayer();
            if (!p) return true;
            if (!RunFor(p) && !previousPhase.count(p->GetGUID().GetCounter())) { handler->SendSysMessage(Text(handler, S_NOT_IN_EXPEDITION, "You are not in an expedition.")); return true; }
            Leave(p);
            return true;
        }
        static bool HandleStatus(ChatHandler* handler)
        {
            ExpeditionsGuardT expeditionsGuard;
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
            ExpeditionsGuardT expeditionsGuard;
            Player* p = handler->GetPlayer();
            if (!p) return true;
            Enter(p, map, tier && tiers.count(*tier) ? *tier : tiers.begin()->first, true);
            return true;
        }
        static bool HandleList(ChatHandler* handler)
        {
            ExpeditionsGuardT expeditionsGuard;
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

        // .temucraft expedition here [name]: this map is an expedition, entered where you stand
        static bool HandleHere(ChatHandler* handler, Optional<Tail> name)
        {
            ExpeditionsGuardT expeditionsGuard;
            Player* p = handler->GetPlayer();
            if (!OnDungeon(handler, p)) return true;
            std::string n = name ? std::string(*name) : std::string();
            WorldDatabase.EscapeString(n);
            WorldDatabase.DirectExecute("INSERT INTO mod_temucraft_expedition (map, name, enabled, entrance_x, entrance_y, entrance_z, entrance_o) "
                "VALUES ({}, '{}', 1, {}, {}, {}, {}) ON DUPLICATE KEY UPDATE name = IF(VALUES(name) = '', name, VALUES(name)), enabled = 1, "
                "entrance_x = VALUES(entrance_x), entrance_y = VALUES(entrance_y), entrance_z = VALUES(entrance_z), entrance_o = VALUES(entrance_o)",
                p->GetMapId(), n, p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), p->GetOrientation());
            LoadData();
            handler->PSendSysMessage("Map {} is an expedition, entered here. Add spots with .temucraft expedition spot normal|elite|boss (target a creature to use its look).",
                p->GetMapId());
            return true;
        }

        // .temucraft expedition spot normal|elite|boss [variant]: an enemy here; its creature is the one you target
        static bool HandleSpot(ChatHandler* handler, std::string kind, Optional<uint8> variant)
        {
            ExpeditionsGuardT expeditionsGuard;
            Player* p = handler->GetPlayer();
            if (!OnDungeon(handler, p)) return true;
            uint8 k = kind == "boss" ? K_BOSS : kind == "elite" ? K_ELITE : kind == "normal" || kind == "mob" ? K_MOB : 255;
            Creature* target = handler->getSelectedCreature();
            if (k == 255 || !target)
            {
                handler->SendSysMessage("Target a creature (its look and spells are used), then: .temucraft expedition spot normal|elite|boss [variant 0-2].");
                return true;
            }
            uint8 v = variant ? std::min<uint8>(*variant, 2) : 0;
            WorldDatabase.DirectExecute("INSERT INTO mod_temucraft_expedition_spawn (map, variant, kind, entry, x, y, z, o) VALUES ({}, {}, {}, {}, {}, {}, {}, {})",
                p->GetMapId(), v, k, target->GetEntry(), p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), p->GetOrientation());
            LoadData();
            handler->PSendSysMessage("Spot added ({} {}, variant {}). .temucraft expedition spots shows the layout.", kind, target->GetName(), v);
            return true;
        }

        // .temucraft expedition unspot: remove the nearest hand-made spot within 10 yards
        static bool HandleUnspot(ChatHandler* handler)
        {
            ExpeditionsGuardT expeditionsGuard;
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
            ExpeditionsGuardT expeditionsGuard;
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
                "Not usable yet: it needs .temucraft expedition here (an entrance) and at least one spot.");
            return true;
        }

        // the relics in use and this run's powers, with their real numbers
        static bool HandlePowers(ChatHandler* handler)
        {
            ExpeditionsGuardT expeditionsGuard;
            Player* p = handler->GetPlayer();
            if (!p) return true;
            CharState& s = State(p);
            handler->PSendSysMessage(Text(handler, S_RELIC_EQUIPPED, "Equipped: {} (combat), {} (utility)"), RelicName(p, s.combat), RelicName(p, s.utility));
            for (uint32 id : { s.combat, s.utility })
                if (Relic const* r = RelicById(id))
                    if (uint8 rank = s.relics.count(id) ? s.relics[id] : 0)
                        handler->PSendSysMessage(Text(handler, S_POWER_LINE, "  {} x{}: {}"), r->name, rank, EffectText(r->text, r->value[rank - 1]));
            Run* run = RunFor(p);
            auto mine = run ? run->powers.find(p->GetGUID().GetCounter()) : decltype(run->powers.end())();
            if (!run || mine == run->powers.end() || mine->second.empty()) { handler->SendSysMessage(Text(handler, S_NO_POWERS, "No expedition powers this run.")); return true; }
            handler->SendSysMessage(Text(handler, S_POWER_HEAD, "Expedition powers this run:"));
            for (auto const& [id, stacks] : mine->second)
                if (Power const* pw = PowerById(id))
                    handler->PSendSysMessage(Text(handler, S_POWER_LINE, "  {} x{}: {}"), pw->name, stacks, EffectText(pw->text, pw->value * int32(stacks)));
            return true;
        }
        static bool HandleRelics(ChatHandler* handler)
        {
            ExpeditionsGuardT expeditionsGuard;
            Player* p = handler->GetPlayer();
            if (!p) return true;
            CharState& s = State(p);
            for (auto const& [id, r] : relicList)
            {
                uint8 rank = s.relics.count(id) ? s.relics[id] : 0;
                if (rank)
                    handler->PSendSysMessage(Text(handler, S_RELIC_LINE, "[{}] {} - rank {}/{}: {}{}"), SlotName(p, r.slot), r.name, rank, MAX_RELIC_RANK,
                        EffectText(r.text, r.value[rank - 1]), s.combat == id || s.utility == id ? std::string(Text(handler, S_RELIC_EQUIPPED_MARK, " (equipped)")) : std::string());
                else
                    handler->PSendSysMessage(Text(handler, S_RELIC_LOCKED, "[{}] {} - not found yet: {}"), SlotName(p, r.slot), r.name, EffectText(r.text, r.value[0]));
            }
            return true;
        }
        static bool HandleLedger(ChatHandler* handler)
        {
            ExpeditionsGuardT expeditionsGuard;
            Player* p = handler->GetPlayer();
            if (!p) return true;
            CharState& s = State(p);
            handler->PSendSysMessage(Text(handler, S_LEDGER, "Wayfinder's Ledger (season {}): rank {}, {} points"), cfg.season, s.rank, s.points);
            auto next = ledgerRanks.upper_bound(s.rank);
            if (next != ledgerRanks.end())
                handler->PSendSysMessage(Text(handler, S_LEDGER_RANK, "Rank {} at {} points: {}{}"), next->first, next->second.points, LedgerRewardText(p, next->second), "");
            handler->PSendSysMessage(Text(handler, S_WALLET_COINS, "{}: {}"), CoinName(), CoinCount(p));
            return true;
        }
        static bool HandleNemesis(ChatHandler* handler)
        {
            ExpeditionsGuardT expeditionsGuard;
            Player* p = handler->GetPlayer();
            Run* run = p ? RunFor(p) : nullptr;
            if (!run) { handler->SendSysMessage(Text(handler, S_NOT_IN_EXPEDITION, "You are not in an expedition.")); return true; }
            if (Creature* n = SpawnNemesis(p->GetMap(), *run, p)) handler->PSendSysMessage("{} spawned.", n->GetName());
            else handler->SendSysMessage("No nemesis: it is off, already here, or the run is over.");
            return true;
        }

        static bool HandleReload(ChatHandler* handler)
        {
            ExpeditionsGuardT expeditionsGuard;
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
    new npc_temucraft_expedition_quartermaster();
    new go_temucraft_expedition_disturbance();
    new go_temucraft_expedition_power_chest();
    new go_temucraft_expedition_trove();
    new item_temucraft_expedition_lure();
    new item_temucraft_expedition_bounty();
    new go_temucraft_expedition_gravestone();
    new npc_temucraft_expedition_nemesis();
}
