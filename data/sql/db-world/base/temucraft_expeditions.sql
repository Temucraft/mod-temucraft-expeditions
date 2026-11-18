-- mod-temucraft-expeditions: expedition overrides, hand-made layouts, tiers, the Expedition Master, the Expedition Cache and the texts

-- Every 5-player dungeon is an expedition on its own (Temucraft.Expeditions.AllDungeons). A row here adds one (any dungeon map,
-- also raids or scenarios) or overrides it: name ('' = the map's name), enabled, min_level (0 = Temucraft.Expeditions.MinLevel),
-- entrance (0,0,0 = the dungeon's own entrance area trigger), enemies (0 = Temucraft.Expeditions.Enemies),
-- boss_entry (0 = the creature on the farthest elite spot), category (255 = by map; 0 classic, 1 Burning Crusade,
-- 2 Wrath, 3 Cataclysm, 4 Mists, 5 scenarios, 6 Temucraft / other).
CREATE TABLE IF NOT EXISTS `mod_temucraft_expedition` (
  `map` INT UNSIGNED NOT NULL,
  `name` VARCHAR(100) NOT NULL DEFAULT '',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `min_level` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `entrance_x` FLOAT NOT NULL DEFAULT 0,
  `entrance_y` FLOAT NOT NULL DEFAULT 0,
  `entrance_z` FLOAT NOT NULL DEFAULT 0,
  `entrance_o` FLOAT NOT NULL DEFAULT 0,
  `enemies` INT UNSIGNED NOT NULL DEFAULT 0,
  `boss_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `category` TINYINT UNSIGNED NOT NULL DEFAULT 255,
  PRIMARY KEY (`map`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Hand-made layouts: when an expedition has rows for today's variant (1..Temucraft.Expeditions.Variants) or variant 0 (every day),
-- they replace the automatic layout. kind: 0 enemy, 1 elite enemy, 2 the expedition boss (one per layout), 3 reserved (cache).
CREATE TABLE IF NOT EXISTS `mod_temucraft_expedition_spawn` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `map` INT UNSIGNED NOT NULL,
  `variant` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `kind` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `entry` INT UNSIGNED NOT NULL,
  `x` FLOAT NOT NULL, `y` FLOAT NOT NULL, `z` FLOAT NOT NULL, `o` FLOAT NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`), KEY `map` (`map`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Tiers: health / damage multipliers, lives (0 = unlimited; one more death than this fails the run), cache rewards
-- (gold in copper; item only when at most half the reward was lost to deaths; bonus item only for a deathless run).
CREATE TABLE IF NOT EXISTS `mod_temucraft_expedition_tier` (
  `tier` INT UNSIGNED NOT NULL,
  `health` FLOAT NOT NULL DEFAULT 1,
  `damage` FLOAT NOT NULL DEFAULT 1,
  `lives` INT UNSIGNED NOT NULL DEFAULT 0,
  `gold` INT UNSIGNED NOT NULL DEFAULT 0,
  `item` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `bonus_item` INT UNSIGNED NOT NULL DEFAULT 0,
  `bonus_count` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`tier`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 11 tiers like The War Within. Rewards: Emblems of Heroism (40752), Valor (40753), Conquest (45624), Triumph (47241),
-- Frost (49426); Frozen Orb (43102) for a deathless run.
DELETE FROM `mod_temucraft_expedition_tier`;
INSERT INTO `mod_temucraft_expedition_tier` (`tier`, `health`, `damage`, `lives`, `gold`, `item`, `item_count`, `bonus_item`, `bonus_count`) VALUES
(1,  0.60, 0.60, 0,  100000, 40752, 1, 0,     0),
(2,  0.75, 0.70, 0,  150000, 40752, 2, 0,     0),
(3,  0.90, 0.80, 0,  200000, 40753, 2, 0,     0),
(4,  1.00, 0.90, 5,  300000, 40753, 3, 43102, 1),
(5,  1.15, 1.00, 5,  400000, 45624, 2, 43102, 1),
(6,  1.30, 1.10, 4,  500000, 45624, 3, 43102, 1),
(7,  1.50, 1.20, 4,  650000, 47241, 2, 43102, 2),
(8,  1.75, 1.35, 3,  800000, 47241, 3, 43102, 2),
(9,  2.00, 1.50, 3, 1000000, 49426, 1, 43102, 2),
(10, 2.40, 1.70, 2, 1250000, 49426, 2, 43102, 3),
(11, 3.00, 2.00, 2, 1500000, 49426, 3, 43102, 3);

-- the scenario maps have no entrance trigger
DELETE FROM `mod_temucraft_expedition` WHERE `map` IN (1048);
INSERT INTO `mod_temucraft_expedition` (`map`, `name`, `entrance_x`, `entrance_y`, `entrance_z`, `entrance_o`, `category`) VALUES
(1048, 'Unga Ingoo', -3092, 744, 1.4, 0.9, 5);

-- the Expedition Master
DELETE FROM `creature_template` WHERE `entry` = 608805;
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`,
  `speed_walk`, `speed_run`, `rank`, `unit_class`, `unit_flags`, `type`, `type_flags`, `AIName`, `MovementType`, `HealthModifier`, `RegenHealth`,
  `flags_extra`, `ScriptName`) VALUES
(608805, 'Thessaly Deepmark', 'Expedition Master', '', 0, 80, 80, 2, 35, 1, 1, 1.14286, 0, 1, 768, 7, 0, '', 0, 1, 1, 2, 'npc_temucraft_expedition_keeper');
DELETE FROM `creature_template_model` WHERE `CreatureID` = 608805;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
SELECT 608805, 0, `CreatureDisplayID`, 1, 1, 0 FROM `creature_template_model` WHERE `CreatureID` = 1356 AND `Idx` = 0;

DELETE FROM `npc_text` WHERE `ID` IN (608805, 608806);
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `Probability0`) VALUES
(608805, 'Every dungeon has a few more secrets once the regulars have gone home. Pick one and how much trouble you want, and I will take you down there.', '', 1),
(608806, 'A sturdy cache, its lock already sprung.', '', 1);

-- the Expedition Cache (goober, opened through its gossip)
DELETE FROM `gameobject_template` WHERE `entry` = 608806;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, `ScriptName`) VALUES
(608806, 10, 259, 'Expedition Cache', '', '', '', 1.6, 'go_temucraft_expedition_cache');

DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-expeditions';
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-expeditions', 1, 'Expeditions are closed.'),
('mod-temucraft-expeditions', 2, 'Today''s rich expeditions ({})'),
('mod-temucraft-expeditions', 3, '{} ({})'),
('mod-temucraft-expeditions', 4, 'This week''s affixes: {} (from tier {}, both from tier {})'),
('mod-temucraft-expeditions', 5, 'No affixes this week.'),
('mod-temucraft-expeditions', 6, 'Back'),
('mod-temucraft-expeditions', 7, 'More...'),
('mod-temucraft-expeditions', 8, '{}{} - level {}+, best tier {}'),
('mod-temucraft-expeditions', 9, 'Tier {}: {} lives, {} gold{}'),
('mod-temucraft-expeditions', 10, 'Tier {} - finish tier {} first'),
('mod-temucraft-expeditions', 11, 'Everyone needs to be level {} for this expedition.'),
('mod-temucraft-expeditions', 12, 'Only the party leader can choose the expedition.'),
('mod-temucraft-expeditions', 13, 'Expeditions are for up to {} players.'),
('mod-temucraft-expeditions', 14, 'Expeditions are for parties, not raids.'),
('mod-temucraft-expeditions', 15, '|cffffcc00Expedition:|r {} - tier {}{}. Defeat the expedition boss deep inside; {} enemies await.'),
('mod-temucraft-expeditions', 16, '|cffffcc00Expedition complete!|r Open the Expedition Cache for your rewards.'),
('mod-temucraft-expeditions', 17, '|cffffcc00Expedition:|r a life is lost ({} of {} left). Each death lowers the cache.'),
('mod-temucraft-expeditions', 18, '|cffffcc00Expedition failed:|r you ran out of lives. Use .expedition leave to go back.'),
('mod-temucraft-expeditions', 19, 'Open the cache'),
('mod-temucraft-expeditions', 20, 'Leave the expedition'),
('mod-temucraft-expeditions', 21, 'The cache stays shut until the expedition boss is defeated.'),
('mod-temucraft-expeditions', 22, 'You already opened your cache.'),
('mod-temucraft-expeditions', 23, '  Cache: {} gold.'),
('mod-temucraft-expeditions', 24, '  Cache: {} x{}.'),
('mod-temucraft-expeditions', 25, '  Rich cache: {} gold.'),
('mod-temucraft-expeditions', 26, '  The rich cache needs a {} to open.'),
('mod-temucraft-expeditions', 27, 'You are not in an expedition.'),
('mod-temucraft-expeditions', 28, '{} - tier {}: {}/{} enemies defeated, {} deaths{}{}'),
('mod-temucraft-expeditions', 29, 'There is no such expedition.'),
('mod-temucraft-expeditions', 30, ' |cff996600(rich)|r'),
('mod-temucraft-expeditions', 32, 'No rich expeditions today.'),
('mod-temucraft-expeditions', 33, '  Tier {} unlocked for this expedition.'),
('mod-temucraft-expeditions', 34, 'Someone in your party is inside a dungeon. Leave it first.'),
('mod-temucraft-expeditions', 35, '  Cache Key Fragments: +{} ({}/{}).'),
('mod-temucraft-expeditions', 36, '  You restore a Cache Key! Keys: {}.'),
('mod-temucraft-expeditions', 37, '  The rich cache needs an Expedition Key ({}/{} shards).'),
('mod-temucraft-expeditions', 38, 'Expedition Keys: {}, Cache Key Fragments: {}/{}.');
-- Translations go in `module_string_locale` (`module`, `id`, `locale`, `string`).
