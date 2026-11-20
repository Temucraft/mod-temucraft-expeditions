-- mod-temucraft-expeditions: nemesis, relics, expedition powers, the Wayfinder's Ledger, Wayfinder Coins and their
-- vendor, bounty maps and Hidden Troves. Re-runnable: tables are created if missing, the module's own rows are
-- deleted and inserted again.
-- Ids: this file uses the block 608900-608939 (creatures, game objects, items and npc_text share the numbers).
-- No new spells: relics and powers use stock 3.3.5 auras; the module sets their amounts in code.

SET @NEMESIS        := 608900;  -- creature: the season's nemesis
SET @QUARTERMASTER  := 608901;  -- creature: Wayfinder Quartermaster (wares, relics, ledger)
SET @DISTURBANCE    := 608902;  -- game object: Strange Disturbance (choose an expedition power)
SET @POWER_CHEST    := 608903;  -- game object: chest with a random expedition power and a few coins
SET @TROVE          := 608904;  -- game object: Hidden Trove (bounty map reward)
SET @COIN           := 608910;  -- item: Wayfinder Coin (the seasonal currency)
SET @LURE           := 608911;  -- item: Hollow Lure (calls the nemesis into a run)
SET @BOUNTY         := 608912;  -- item: Wayfinder's Bounty Map (one Hidden Trove a week)
SET @DUMMY_SPELL    := 18282;   -- stock "Dummy Spell": the client needs a use spell; the module handles the use
-- stock auras the relics and powers use (single effect, no spell script on them)
SET @AURA_DAMAGE    := 33779;   -- Twin Spire Blessing: damage done %
SET @AURA_TAKEN     := 33962;   -- Toughen: damage taken %
SET @AURA_SHIELD    := 29503;   -- Lesser Warding Shield: absorb
SET @AURA_HEALING   := 62519;   -- Attuned to Nature: healing received %
SET @AURA_SPEED     := 56489;   -- Warming Up: movement speed %

-- Relics: slot 0 combat, 1 utility. effect: 1 damage done %, 2 damage taken % (lower), 3 shield (% of health,
-- renewed after it breaks), 4 healing received %, 5 movement speed %, 6 more Wayfinder Coins from caches %,
-- 7 chance of a second stack per expedition power %. target: 0 the player, 1 the companion. value1-4: the amount at
-- ranks 1-4 ('{}' in the description shows it). unlock_cost: coins for rank 1 (0 = only found in caches and troves),
-- cost2-4: coins for ranks 2-4. spell 0 = handled in code, no aura.
CREATE TABLE IF NOT EXISTS `mod_temucraft_expedition_relic` (
  `id` INT UNSIGNED NOT NULL,
  `slot` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `name` VARCHAR(100) NOT NULL DEFAULT '',
  `description` VARCHAR(255) NOT NULL DEFAULT '',
  `effect` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `target` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `spell` INT UNSIGNED NOT NULL DEFAULT 0,
  `value1` INT NOT NULL DEFAULT 0,
  `value2` INT NOT NULL DEFAULT 0,
  `value3` INT NOT NULL DEFAULT 0,
  `value4` INT NOT NULL DEFAULT 0,
  `unlock_cost` INT UNSIGNED NOT NULL DEFAULT 0,
  `cost2` INT UNSIGNED NOT NULL DEFAULT 0,
  `cost3` INT UNSIGNED NOT NULL DEFAULT 0,
  `cost4` INT UNSIGNED NOT NULL DEFAULT 0,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `mod_temucraft_expedition_relic` WHERE `id` BETWEEN 1 AND 99;
INSERT INTO `mod_temucraft_expedition_relic` (`id`, `slot`, `name`, `description`, `effect`, `target`, `spell`, `value1`, `value2`, `value3`, `value4`, `unlock_cost`, `cost2`, `cost3`, `cost4`) VALUES
(1, 0, 'Emberfang Talisman',  'damage done +{}%',                                     1, 0, @AURA_DAMAGE,   3,  5,  7, 10, 100, 150, 250, 400),
(2, 0, 'Bulwark Idol',        'damage taken -{}%',                                    2, 0, @AURA_TAKEN,    3,  5,  7, 10, 100, 150, 250, 400),
(3, 0, 'Wardstone Shard',     'a shield worth {}% of your health, back after it breaks', 3, 0, @AURA_SHIELD, 5,  8, 11, 15, 100, 150, 250, 400),
(4, 0, 'Verdant Seed',        'healing received +{}%',                                4, 0, @AURA_HEALING,  5,  8, 12, 16, 100, 150, 250, 400),
(5, 1, 'Trailblazer''s Boots', 'movement speed +{}%',                                 5, 0, @AURA_SPEED,    5,  8, 11, 15,  80, 120, 200, 300),
(6, 1, 'Gilded Compass',      'Wayfinder Coins from caches +{}%',                     6, 0, 0,             10, 20, 30, 40,  80, 120, 200, 300),
(7, 1, 'Four-Leaf Token',     '{}% chance of a second stack for each expedition power', 7, 0, 0,          10, 15, 20, 25,  80, 120, 200, 300),
(8, 1, 'Companion''s Banner',  'your companion deals {}% more damage',                1, 1, @AURA_DAMAGE,   5, 10, 15, 20,  80, 120, 200, 300);

-- Expedition powers: the same effects as relics, value per stack, max_stacks (0 = no limit), weight = how often
-- it is offered compared to the others.
CREATE TABLE IF NOT EXISTS `mod_temucraft_expedition_power` (
  `id` INT UNSIGNED NOT NULL,
  `name` VARCHAR(100) NOT NULL DEFAULT '',
  `description` VARCHAR(255) NOT NULL DEFAULT '',
  `effect` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `target` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `spell` INT UNSIGNED NOT NULL DEFAULT 0,
  `value` INT NOT NULL DEFAULT 0,
  `max_stacks` INT UNSIGNED NOT NULL DEFAULT 0,
  `weight` INT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `mod_temucraft_expedition_power` WHERE `id` BETWEEN 1 AND 99;
INSERT INTO `mod_temucraft_expedition_power` (`id`, `name`, `description`, `effect`, `target`, `spell`, `value`, `max_stacks`, `weight`) VALUES
(1, 'Ember Surge',  'damage done +{}%',                         1, 0, @AURA_DAMAGE,  2, 10, 10),
(2, 'Stoneskin',    'damage taken -{}%',                        2, 0, @AURA_TAKEN,   2, 10,  8),
(3, 'Second Wind',  'healing received +{}%',                    4, 0, @AURA_HEALING, 4,  5,  6),
(4, 'Fleet Foot',   'movement speed +{}%',                      5, 0, @AURA_SPEED,   4,  5,  5),
(5, 'Barrier',      'a shield worth {}% of your health, back after it breaks', 3, 0, @AURA_SHIELD, 5, 4, 5),
(6, 'Kindred Fury', 'your companion deals {}% more damage',     1, 1, @AURA_DAMAGE,  5,  5,  4),
(7, 'Coin Sense',   'Wayfinder Coins from the cache +{}%',      6, 0, 0,            10,  5,  3);

-- Wayfinder's Ledger: points needed (in total) for each rank and its reward (item, Wayfinder Coins, gold in copper).
-- Points come from caches (Temucraft.Expeditions.Ledger.PointsPerTier x tier), the nemesis and the nemesis lair.
CREATE TABLE IF NOT EXISTS `mod_temucraft_expedition_ledger_rank` (
  `rank` INT UNSIGNED NOT NULL,
  `points` INT UNSIGNED NOT NULL DEFAULT 0,
  `item` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `coins` INT UNSIGNED NOT NULL DEFAULT 0,
  `gold` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`rank`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `mod_temucraft_expedition_ledger_rank`;
INSERT INTO `mod_temucraft_expedition_ledger_rank` (`rank`, `points`, `item`, `item_count`, `coins`, `gold`) VALUES
(1,   100, 0,       0,  20,       0),
(2,   250, @BOUNTY, 1,   0,       0),
(3,   450, 0,       0,  40,       0),
(4,   700, 43102,   2,   0,       0),  -- Frozen Orb
(5,  1000, @LURE,   1,   0,       0),
(6,  1400, 0,       0,  60,  500000),
(7,  1900, 49426,   5,   0,       0),  -- Emblem of Frost
(8,  2500, 0,       0, 100,       0),
(9,  3200, @BOUNTY, 1,   0, 1000000),
(10, 4000, 49426,  10, 200,       0);

-- Wares of the Wayfinder Quartermaster, paid in Wayfinder Coins (Temucraft.Expeditions.Currency.Item).
-- ledger_rank: the Ledger rank needed to buy (0 = none).
CREATE TABLE IF NOT EXISTS `mod_temucraft_expedition_vendor` (
  `id` INT UNSIGNED NOT NULL,
  `item` INT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 1,
  `cost` INT UNSIGNED NOT NULL DEFAULT 0,
  `ledger_rank` INT UNSIGNED NOT NULL DEFAULT 0,
  `sort` INT UNSIGNED NOT NULL DEFAULT 0,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `mod_temucraft_expedition_vendor` WHERE `id` BETWEEN 1 AND 99;
INSERT INTO `mod_temucraft_expedition_vendor` (`id`, `item`, `item_count`, `cost`, `ledger_rank`, `sort`) VALUES
(1, @BOUNTY, 1,  60, 0, 1),
(2, @LURE,   1, 150, 3, 2),
(3, 43102,   1,  30, 0, 3),  -- Frozen Orb
(4, 47241,   1,  25, 0, 4),  -- Emblem of Triumph
(5, 49426,   1,  50, 5, 5);  -- Emblem of Frost

-- The nemesis lair's two hidden tiers (shown as ? and ??): base_tier is the ordinary tier used for the enemies,
-- lives and the cache; health / damage multiply the nemesis on top of Temucraft.Expeditions.Nemesis.Health / Damage;
-- item, coins and ledger points are the lair's own reward.
CREATE TABLE IF NOT EXISTS `mod_temucraft_expedition_nemesis_tier` (
  `hidden_tier` TINYINT UNSIGNED NOT NULL,
  `base_tier` INT UNSIGNED NOT NULL DEFAULT 8,
  `health` FLOAT NOT NULL DEFAULT 1,
  `damage` FLOAT NOT NULL DEFAULT 1,
  `item` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `coins` INT UNSIGNED NOT NULL DEFAULT 0,
  `ledger_points` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`hidden_tier`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `mod_temucraft_expedition_nemesis_tier`;
INSERT INTO `mod_temucraft_expedition_nemesis_tier` (`hidden_tier`, `base_tier`, `health`, `damage`, `item`, `item_count`, `coins`, `ledger_points`) VALUES
(1, 8,  1.0, 1.00, 49426, 3, 100, 150),
(2, 11, 1.4, 1.25, 49426, 6, 250, 300);

-- the nemesis (look of Blackheart the Inciter; Temucraft.Expeditions.Nemesis.DisplayId can change it) and the quartermaster
DELETE FROM `creature_template` WHERE `entry` IN (@NEMESIS, @QUARTERMASTER);
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`,
  `speed_walk`, `speed_run`, `rank`, `unit_class`, `unit_flags`, `type`, `type_flags`, `AIName`, `MovementType`, `HealthModifier`, `RegenHealth`,
  `flags_extra`, `ScriptName`) VALUES
(@NEMESIS, 'Vorrath the Hollow Stalker', 'Expedition Nemesis', '', 0, 60, 60, 0, 14, 0, 1, 1.14286, 1, 1, 0, 3, 0, '', 0, 1, 1, 0, 'npc_temucraft_expedition_nemesis'),
(@QUARTERMASTER, 'Odette Coinwell', 'Wayfinder Quartermaster', '', 0, 80, 80, 2, 35, 1, 1, 1.14286, 0, 1, 768, 7, 0, '', 0, 1, 1, 2, 'npc_temucraft_expedition_quartermaster');
DELETE FROM `creature_template_model` WHERE `CreatureID` IN (@NEMESIS, @QUARTERMASTER);
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
SELECT @NEMESIS, 0, `CreatureDisplayID`, 1, 1, 0 FROM `creature_template_model` WHERE `CreatureID` = 18667 AND `Idx` = 0;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
SELECT @QUARTERMASTER, 0, `CreatureDisplayID`, 1, 1, 0 FROM `creature_template_model` WHERE `CreatureID` = 2785 AND `Idx` = 0;

-- Strange Disturbance, power chest and Hidden Trove (goobers, opened through their gossip)
DELETE FROM `gameobject_template` WHERE `entry` IN (@DISTURBANCE, @POWER_CHEST, @TROVE);
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, `ScriptName`) VALUES
(@DISTURBANCE, 10, 7118, 'Strange Disturbance', '', '', '', 1.0, 'go_temucraft_expedition_disturbance'),
(@POWER_CHEST, 10, 10,   'Wayfinder''s Chest',  '', '', '', 1.0, 'go_temucraft_expedition_power_chest'),
(@TROVE,       10, 1387, 'Hidden Trove',        '', '', '', 1.2, 'go_temucraft_expedition_trove');

DELETE FROM `npc_text` WHERE `ID` BETWEEN @QUARTERMASTER AND @TROVE;
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `Probability0`) VALUES
(@QUARTERMASTER, 'Coins from the deep places spend just as well up here. Show me what you found, and let us see about those relics of yours.', '', 1),
(@DISTURBANCE, 'Something hums inside the swirling dark. It offers a little of its strength, for as long as you stay down here.', '', 1),
(@POWER_CHEST, 'A battered chest left by some earlier wayfinder.', '', 1),
(@TROVE, 'The bounty map was right: a trove nobody has touched in years.', '', 1);

-- items: the seasonal currency, the lure and the bounty map
DELETE FROM `item_template` WHERE `entry` IN (@COIN, @LURE, @BOUNTY);
INSERT INTO `item_template` (`entry`, `class`, `subclass`, `SoundOverrideSubclass`, `name`, `displayid`, `Quality`, `Flags`, `BuyCount`, `BuyPrice`,
  `SellPrice`, `InventoryType`, `ItemLevel`, `RequiredLevel`, `maxcount`, `stackable`, `spellid_1`, `spelltrigger_1`, `spellcharges_1`,
  `spellcooldown_1`, `spellcategory_1`, `spellcategorycooldown_1`, `bonding`, `description`, `Material`, `BagFamily`, `ScriptName`, `VerifiedBuild`) VALUES
(@COIN,   10, 0, -1, 'Wayfinder Coin', 34104, 2, 0, 1, 0, 0, 0, 1, 0, 0, 2147483647, 0, 0, 0, -1, 0, -1, 1,
  'Found in expedition caches this season. Spent with the Wayfinder Quartermaster and on relics.', -1, 8192, '', 0),
(@LURE,    0, 8, -1, 'Hollow Lure', 13121, 3, 0, 1, 0, 0, 0, 60, 10, 0, 5, @DUMMY_SPELL, 0, 0, 1000, 0, 0, 1,
  'Use inside an expedition to call this season''s nemesis to you. Once per run.', -1, 0, 'item_temucraft_expedition_lure', 0),
(@BOUNTY,  0, 8, -1, 'Wayfinder''s Bounty Map', 23229, 2, 0, 1, 0, 0, 0, 60, 10, 1, 1, @DUMMY_SPELL, 0, 0, 1000, 0, 0, 1,
  'Use inside an expedition: a Hidden Trove appears when its boss falls. One bounty a week.', -1, 0, 'item_temucraft_expedition_bounty', 0);

-- texts 40 and up (1-38 are in temucraft_expeditions.sql)
DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-expeditions' AND `id` BETWEEN 40 AND 199;
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-expeditions', 40, '{}''s Lair - tier {}'),
('mod-temucraft-expeditions', 41, '{}''s Lair - tier ?: finish tier {} of any expedition first'),
('mod-temucraft-expeditions', 42, '{}''s Lair - tier ??: defeat tier ? first'),
('mod-temucraft-expeditions', 43, '|cffff3333{} ambushes you!|r Defeat it for a nemesis reward, or press on.'),
('mod-temucraft-expeditions', 44, '|cffff3333The lure sounds. {} answers the call!|r'),
('mod-temucraft-expeditions', 45, '|cffffcc00Nemesis defeated:|r {} falls.'),
('mod-temucraft-expeditions', 46, 'The lure only works inside an expedition that is still running, once per run, and not in the nemesis lair.'),
('mod-temucraft-expeditions', 47, '|cff66ccffExpedition power:|r {} x{} ({}).'),
('mod-temucraft-expeditions', 48, 'Expedition powers this run:'),
('mod-temucraft-expeditions', 49, '  {} x{}: {}'),
('mod-temucraft-expeditions', 50, 'No expedition powers this run.'),
('mod-temucraft-expeditions', 51, 'Take: {} - {}'),
('mod-temucraft-expeditions', 52, 'The disturbance has gone quiet for you.'),
('mod-temucraft-expeditions', 53, 'Relics'),
('mod-temucraft-expeditions', 54, 'Equipped: {} (combat), {} (utility)'),
('mod-temucraft-expeditions', 55, '[{}] {} - rank {}/{}: {}{}'),
('mod-temucraft-expeditions', 56, '[{}] {} - not found yet: {}'),
('mod-temucraft-expeditions', 57, 'Equip {}'),
('mod-temucraft-expeditions', 58, 'Upgrade to rank {} ({}) for {} {}'),
('mod-temucraft-expeditions', 59, 'Unlock rank 1 ({}) for {} {}'),
('mod-temucraft-expeditions', 60, 'You need {} {} for that.'),
('mod-temucraft-expeditions', 61, '{} is now rank {}.'),
('mod-temucraft-expeditions', 62, '  |cffa335eeRelic found:|r {}!'),
('mod-temucraft-expeditions', 63, '{} equipped in the {} slot.'),
('mod-temucraft-expeditions', 64, 'Wayfinder''s Ledger (season {}): rank {}, {} points'),
('mod-temucraft-expeditions', 65, 'Rank {} at {} points: {}{}'),
('mod-temucraft-expeditions', 66, '  Wayfinder''s Ledger: +{} points ({} in total).'),
('mod-temucraft-expeditions', 67, '  |cffffcc00Wayfinder''s Ledger rank {}!|r'),
('mod-temucraft-expeditions', 68, 'Wayfinder Quartermaster wares'),
('mod-temucraft-expeditions', 69, '{} x{} - {} {}'),
('mod-temucraft-expeditions', 70, 'Bought {} x{}.'),
('mod-temucraft-expeditions', 71, ' (Ledger rank {})'),
('mod-temucraft-expeditions', 72, 'Buy {} x{} for {} {}?'),
('mod-temucraft-expeditions', 73, 'The map shows a Hidden Trove in this expedition. It opens when the expedition boss falls.'),
('mod-temucraft-expeditions', 74, 'You already followed a bounty map this week.'),
('mod-temucraft-expeditions', 75, 'Use the bounty map inside an expedition that is still running.'),
('mod-temucraft-expeditions', 76, 'Open the Hidden Trove'),
('mod-temucraft-expeditions', 77, 'This trove was found with someone else''s map.'),
('mod-temucraft-expeditions', 78, '|cffffcc00A Hidden Trove appears!|r'),
('mod-temucraft-expeditions', 79, 'combat'),
('mod-temucraft-expeditions', 80, 'utility'),
('mod-temucraft-expeditions', 81, 'none'),
('mod-temucraft-expeditions', 82, 'Open the chest'),
('mod-temucraft-expeditions', 83, 'nothing'),
('mod-temucraft-expeditions', 84, 'Expedition rewards'),
('mod-temucraft-expeditions', 85, 'Your bags were full, so these came by mail.'),
('mod-temucraft-expeditions', 86, 'Strange Disturbances and chests inside give expedition powers; they last until you leave.'),
('mod-temucraft-expeditions', 87, ' - every rank reached'),
('mod-temucraft-expeditions', 88, 'This relic is at its highest rank.'),
('mod-temucraft-expeditions', 89, '  Your bags are full: the rest comes by mail.'),
('mod-temucraft-expeditions', 90, ' - next rank at {} points'),
('mod-temucraft-expeditions', 91, ' (equipped)'),
('mod-temucraft-expeditions', 92, '{} taken off.'),
('mod-temucraft-expeditions', 93, 'Take off {}'),
('mod-temucraft-expeditions', 94, '{}: {}');
-- Translations go in `module_string_locale` (`module`, `id`, `locale`, `string`).
