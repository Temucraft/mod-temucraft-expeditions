-- mod-temucraft-expeditions: gravestones, slayer enemies and the addon window. Re-runnable: the module's own rows are
-- deleted and inserted again.
-- Ids: 608905 from this module's block 608900-608939 (the game object and its npc_text share the number).
SET @GRAVESTONE := 608905;   -- game object: a gravestone where a player fell (Temucraft.Expeditions.Gravestone.Entry)

-- the gravestone (stock "Gravestone" model); a party member uses it to raise the player who fell there
DELETE FROM `gameobject_template` WHERE `entry` = @GRAVESTONE;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, `ScriptName`) VALUES
(@GRAVESTONE, 10, 8255, 'Gravestone', '', '', '', 1.0, 'go_temucraft_expedition_gravestone');

DELETE FROM `npc_text` WHERE `ID` = @GRAVESTONE;
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `Probability0`) VALUES
(@GRAVESTONE, 'A fresh gravestone. The wayfinder who fell here is not gone yet.', '', 1);

-- texts (95 and up; temucraft_expeditions_features.sql has 40-94)
DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-expeditions' AND `id` BETWEEN 95 AND 199;
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-expeditions', 95, '{}''s gravestone marks where they fell. A party member can use it to raise them there.'),
('mod-temucraft-expeditions', 96, 'Raise {} here'),
('mod-temucraft-expeditions', 97, '{} raises {} at the gravestone.'),
('mod-temucraft-expeditions', 98, '{} has to be inside the dungeon to be raised here (run back first).'),
('mod-temucraft-expeditions', 99, 'Your party has to use your gravestone to raise you.'),
('mod-temucraft-expeditions', 100, 'There is nobody to raise here.'),
('mod-temucraft-expeditions', 101, 'Slayer {}'),
('mod-temucraft-expeditions', 102, '|cffff3333A {} stalks this expedition.|r It is tougher than the rest and carries extra {}.'),
('mod-temucraft-expeditions', 103, '|cffffcc00{} is slain!|r');
-- Translations go in `module_string_locale` (`module`, `id`, `locale`, `string`).
