-- mod-temucraft-expeditions: each character's best tier and completions per expedition, and the day of its last rich cache
CREATE TABLE IF NOT EXISTS `mod_temucraft_expedition_progress` (
  `owner` INT UNSIGNED NOT NULL COMMENT 'character guid',
  `map` INT UNSIGNED NOT NULL,
  `best_tier` INT UNSIGNED NOT NULL DEFAULT 0,
  `completions` INT UNSIGNED NOT NULL DEFAULT 0,
  `rich_day` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'days since 1970 of the last rich cache opened here',
  PRIMARY KEY (`owner`, `map`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Cache Key Fragments and Expedition Keys (Temucraft.Expeditions.Rich.Keys), kept per character
CREATE TABLE IF NOT EXISTS `mod_temucraft_expedition_wallet` (
  `owner` INT UNSIGNED NOT NULL COMMENT 'character guid',
  `shards` INT UNSIGNED NOT NULL DEFAULT 0,
  `keys` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`owner`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
