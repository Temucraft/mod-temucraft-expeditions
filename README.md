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
| characters | `mod_temucraft_expedition_progress`, `mod_temucraft_expedition_wallet` | new: best tier, completions and last rich day per character and expedition; key fragments and keys |

It reads (never changes) `creature`, `creature_template` and `areatrigger_teleport` to find each dungeon's spots and
entrance.

## Not done yet

Nemesis bosses, relics, expedition powers, the Wayfinder's Ledger and a seasonal currency (see the design notes in the project).

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
