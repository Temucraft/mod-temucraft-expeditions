# mod-temucraft-expeditions

Temucraft expeditions on AzerothCore 3.3.5a, on top of **any dungeon**: talk to the Expedition Master, choose a
dungeon and a tier, and your party is sent in. Inside, the party is moved to the expedition phase, where the dungeon's own
creatures, bosses and doors (phase 1) don't exist, and the expedition spawns its own enemies, scaled to you.

## How it plays

- Place the **Expedition Master** (creature 608805, Thessaly Deepmark) with `.npc add 608805`. Its menu has today's
  rich expeditions, the expeditions by expansion (Classic, Burning Crusade, Wrath, Cataclysm, Mists, scenarios, Temucraft
  and other), and this week's affixes.
- Choose an expedition, then a **tier** (1 to 11). Tier 1 is open; each tier you finish in an expedition opens the next one there.
  Solo, or the party leader takes up to four party members standing nearby (no raids). Everyone has to be outside any
  dungeon. The dungeon is entered on normal difficulty, so no heroic lockout is spent.
- Enemies stand on the dungeon's own creature spots, spread from the entrance to the far end; the **expedition boss** is on
  the farthest elite spot. Which spots are used changes daily (3 story variants). Enemies are set to the party's level,
  then made tougher by kind (normal, elite, boss), tier, party size and affixes.
- **Lives** from tier 4: each death costs part of the cache; one death past the lives fails the run.
- Kill the boss and a **Expedition Cache** appears: gold, the tier's item, a bonus item for a deathless run, and on a
  **rich** expedition (3 a day, the same for everyone) the rich reward for your first rich cache of the day
  which needs a **Expedition Key**: every cache gives Cache Key Fragments (10 × tier), and every 100 shards
  make a key (`.expedition status` shows them; a real key item can be used instead). The cache also takes you back to where you started.
- **Affixes** change weekly, one from tier 4, two from tier 8: Hardened (enemies +25% health), Warlord's (boss +30%
  health, +15% damage), Brutal (+15% damage), Swarming (+30% enemies).
- **Companions** from [mod-temucraft-followers](https://github.com/Temucraft/mod-temucraft-followers) come along when their activity mask has the expedition bit (4).
- **Wayfinder Coins** (item 608910) are the seasonal currency: every cache gives 5 per tier (fewer after deaths). The
  **Wayfinder Quartermaster** (creature 608901, Odette Coinwell, place her next to the Expedition Master with
  `.npc add 608901`) sells bounty maps, lures and emblems for them and looks after relics.
- **Nemesis**: this season's nemesis is **Vorrath the Hollow Stalker** (creature 608900). From tier 6 each run has a 15%
  chance that he ambushes the party once half the enemies are down; a **Hollow Lure** (608911, from caches or the
  quartermaster) calls him into a run on purpose. Killing him is optional and gives coins and ledger points. He also has
  his own **lair** (Shadowfang Keep by default) with two hidden tiers, **?** (opens after tier 8 of any expedition) and
  **??** (after ?), where he is the boss. The Expedition Master offers the lair.
- **Relics**: one **combat** and one **utility** relic per character, ranks 1 to 4. Unlock and upgrade them with
  Wayfinder Coins (Relics at the Expedition Master or the quartermaster); caches and troves sometimes give one you do not
  have. They only work inside an expedition. Combat: Emberfang Talisman (damage done), Bulwark Idol (damage taken),
  Wardstone Shard (a shield that comes back), Verdant Seed (healing received). Utility: Trailblazer's Boots (speed),
  Gilded Compass (more coins), Four-Leaf Token (double powers), Companion's Banner (companion damage).
- **Expedition powers**: each run has 2 **Strange Disturbances** (pick one of 3 powers) and 2 **Wayfinder's Chests** (a
  random power and a few coins) next to enemy spots. Powers stack during the run (Ember Surge, Stoneskin, Second Wind,
  Fleet Foot, Barrier, Kindred Fury, Coin Sense) and are gone when you leave the expedition. A corpse run keeps them.
- **Wayfinder's Ledger**: a seasonal track. Caches give 10 points per tier, the nemesis 50 and his lair more; each of the
  10 ranks has a reward (coins, bounty maps, a lure, Frozen Orbs, Emblems of Frost, gold), given at once.
- **Bounty maps** (608912): use one inside a run that is still going (one a week per character) and a **Hidden Trove**
  (608904) appears next to the cache for you: coins, gold and a chance of a relic.
- Relic and power effects are stock 3.3.5 auras (Twin Spire Blessing, Toughen, Lesser Warding Shield, Attuned to Nature,
  Warming Up) with our own amounts set by the module, so no client patch is needed. The buff tooltip shows the stock
  number; the real numbers are in the gossip menus and `.expedition powers`.
- **Gravestones**: a player who dies in a run leaves a **Gravestone** (608905) where they fell. A party member who uses it
  raises them right there (50% health and mana), once per player per run by default. The fallen player has to be in the
  dungeon (not released to the graveyard outside, or run back first). The death still costs a life. Settings:
  `Gravestone.*` (on/off, uses per run, which tiers, the lair, how long it stays, how much health).
- **Slayer enemies**: from tier 3, each run has a 25% chance that one normal enemy is a **slayer**: three times the health,
  half again the damage, a bit bigger, and 10 extra Wayfinder Coins for everyone when it falls. The party is told which
  enemy it is when they enter ("Slayer <name>", `module_string` 101). The client shows the creature's own name, since a
  creature's name comes from its template. Settings: `Slayer.*`.
- **Expedition window**: the TemucraftExpeditions window shows the current run (tier, affixes, enemies left, deaths and
  lives), this run's powers with their stacks, the equipped relics, and the Ledger rank, points and coins. It opens by
  itself when you enter an expedition; `/tex` (or `/wayfinder`) opens and closes it. It is in `addon/TemucraftExpeditions`
  (copy it to `Interface/AddOns`) and is also built into the client by mod-temucraft-client-version (the same file;
  the AddOns copy steps aside then). It talks to the server with addon messages, prefix `TEX`
  (`Temucraft.Expeditions.Addon.Enable`).
- Walking into the dungeon through its normal portal is still the normal dungeon: only the Expedition Master starts an expedition.

## Your own expeditions and layouts

- `mod_temucraft_expedition` adds or changes an expedition: any dungeon map (raids and scenarios too), a name, on/off, a minimum
  level, an entrance, the number of enemies, a fixed boss creature and the category. Unga Ingoo (scenario, map 1048)
  is added this way.
- `mod_temucraft_expedition_spawn` replaces the automatic layout with a hand-made one: per variant (0 = every day), enemies
  (kind 0), elites (1) and the boss (2) with exact positions.
- `mod_temucraft_expedition_tier` holds the tiers: health and damage multipliers, lives, gold and items.
- `.expedition list` shows every expedition with its category and spot count; `.expedition reload` reloads all three tables.

## Commands

| Command | Who | What |
|---|---|---|
| `.expedition leave` | everyone | leave the expedition, back to where you entered it |
| `.expedition status` | everyone | tier, enemies defeated, deaths and lives |
| `.expedition powers` | everyone | equipped relics and this run's powers, with their real numbers |
| `.expedition relics` | everyone | every relic, its rank and effect |
| `.expedition ledger` | everyone | Wayfinder's Ledger rank, points, next reward and coins |
| `.expedition nemesis` | GM | call the nemesis into the run you are in |
| `.expedition start <map> [tier]` | GM | start an expedition here without the Keeper (no level check) |
| `.expedition list` | GM | all expeditions |
| `.expedition reload` | admin | reload expeditions, layouts and tiers |

## Install

1. Put this folder in `modules/`, copy `conf/mod-temucraft-expeditions.conf.dist` to your config folder as
   `mod-temucraft-expeditions.conf`, re-run CMake and build.
2. The SQL in `data/sql/` is applied by the worldserver on its next start.

## What it changes

| Database | Table | What |
|---|---|---|
| world | `mod_temucraft_expedition`, `mod_temucraft_expedition_spawn`, `mod_temucraft_expedition_tier` | new: expedition overrides, hand-made layouts, tiers |
| world | `creature_template`, `creature_template_model`, `npc_text` | the Expedition Master (608805) and its texts |
| world | `gameobject_template` | the Expedition Cache (608806) |
| world | `module_string` | the module's texts |
| world | `mod_temucraft_expedition_relic`, `mod_temucraft_expedition_power`, `mod_temucraft_expedition_ledger_rank`, `mod_temucraft_expedition_vendor`, `mod_temucraft_expedition_nemesis_tier` | new: relics, powers, ledger ranks and rewards, quartermaster wares, the lair's hidden tiers |
| world | `creature_template`, `creature_template_model`, `gameobject_template`, `item_template`, `npc_text` | the nemesis (608900), the quartermaster (608901), Strange Disturbance (608902), Wayfinder's Chest (608903), Hidden Trove (608904), Wayfinder Coin (608910), Hollow Lure (608911), Wayfinder's Bounty Map (608912) |
| world | `gameobject_template`, `npc_text`, `module_string` | the Gravestone (608905) and the texts 95-103 (`temucraft_expeditions_extras.sql`) |
| characters | `mod_temucraft_expedition_progress`, `mod_temucraft_expedition_wallet` | new: best tier, completions and last rich day per character and expedition; key fragments and keys |
| characters | `mod_temucraft_expedition_relic`, `mod_temucraft_expedition_character`, `mod_temucraft_expedition_ledger` | new: relics and ranks, the equipped pair and bounty week, ledger points per season |
| auth | `rbac_permissions`, `rbac_linked_permissions` | GM command permissions 100501-100508 |

The new SQL is in its own files (`temucraft_expeditions_features.sql`, `temucraft_expeditions_features_characters.sql`,
`temucraft_expeditions_extras.sql`), so
a server that already has the first files only applies the new ones. Every file can be run again safely.

### Ids

| Block | Used for |
|---|---|
| 608805, 608806 | Expedition Master, Expedition Cache |
| 608900-608939 | this module's second block: 608900-608901 creatures, 608902-608905 game objects (and their `npc_text`), 608910-608912 items; 608906-608909 and 608913-608939 are free for later |

No creature or game object is spawned by the SQL (place the NPCs with `.npc add`), so no GUIDs are used. No new spells:
the item use spell is the stock Dummy Spell (18282) and the auras are stock spells; the module clears their dispel type
in memory so enemies can not dispel them.

It reads (never changes) `creature`, `creature_template` and `areatrigger_teleport` to find each dungeon's spots and
entrance.

## Settings for the new features

Each feature has its own switch and numbers in the config: `Temucraft.Expeditions.Season`, `Currency.*`, `Ledger.*`,
`Vendor.Enable`, `Nemesis.*` (chance, tier, look, spells, lure, reward, lair map), `Relics.*`, `Powers.*`, `Bounty.*`,
`Gravestone.*`, `Slayer.*` and `Addon.Enable`.
Relics, powers, ledger ranks, wares and the lair tiers are rows in the world tables above; `.expedition reload` reads
them again. A new season: raise `Temucraft.Expeditions.Season`, change the ledger ranks and the nemesis if you like.

## Texts and translations

The player-facing texts are in the `module_string` table under the module name `mod-temucraft-expeditions`.
Translations go in `module_string_locale` with the same module and id plus the locale. If a row is missing the
English text in the code is used.

Part of the Temucraft modules for AzerothCore 3.3.5a.

## Building an expedition in game (any dungeon map, also unused ones)

Game masters can turn any dungeon map into an expedition with a hand-made layout, without SQL. This is how maps that
have no creatures of their own (unused or empty dungeon maps) become expeditions.

| Command | What |
|---|---|
| `.expedition here [name]` | This map is an expedition, entered where you stand |
| `.expedition spot normal\|elite\|boss [variant 0-2]` | An enemy where you stand, using the targeted creature's look and spells |
| `.expedition unspot` | Remove the nearest hand-made spot (10 yards) |
| `.expedition spots` | The layout so far, and whether the expedition is usable |

Rows go to `mod_temucraft_expedition` and `mod_temucraft_expedition_spawn`; the module reloads them at once.
