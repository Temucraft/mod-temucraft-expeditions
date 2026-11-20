-- mod-temucraft-expeditions: relics, the equipped pair, the weekly bounty map and the Wayfinder's Ledger per character

-- relics a character has found or bought, and their rank (1-4)
CREATE TABLE IF NOT EXISTS `mod_temucraft_expedition_relic` (
  `owner` INT UNSIGNED NOT NULL COMMENT 'character guid',
  `relic` INT UNSIGNED NOT NULL COMMENT 'mod_temucraft_expedition_relic.id (world)',
  `rank` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`owner`, `relic`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- the equipped combat and utility relic (0 = none) and the week of the last bounty map used
CREATE TABLE IF NOT EXISTS `mod_temucraft_expedition_character` (
  `owner` INT UNSIGNED NOT NULL COMMENT 'character guid',
  `combat_relic` INT UNSIGNED NOT NULL DEFAULT 0,
  `utility_relic` INT UNSIGNED NOT NULL DEFAULT 0,
  `bounty_week` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'weeks since 1970 (Thursday start, like the affixes)',
  PRIMARY KEY (`owner`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Wayfinder's Ledger progress per season (Temucraft.Expeditions.Season)
CREATE TABLE IF NOT EXISTS `mod_temucraft_expedition_ledger` (
  `owner` INT UNSIGNED NOT NULL COMMENT 'character guid',
  `season` INT UNSIGNED NOT NULL,
  `points` INT UNSIGNED NOT NULL DEFAULT 0,
  `rank` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'highest rank reached (its rewards are given)',
  `nemesis_kills` INT UNSIGNED NOT NULL DEFAULT 0,
  `lair_best` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 = tier ? of the nemesis lair done, 2 = tier ??',
  PRIMARY KEY (`owner`, `season`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
