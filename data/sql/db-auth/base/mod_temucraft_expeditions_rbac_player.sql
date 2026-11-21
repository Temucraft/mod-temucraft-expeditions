-- mod-temucraft-expeditions: its commands (all under .temucraft) as RBAC permissions 100550-100599. Every command is admin only by
-- default: linked to the administrator roles 192 and 1014. To give one to players, link it to 195 and 1010 here (or
-- grant it per account with .rbac account grant <account> <id>). "Temucraft: use ..." permissions are not
-- commands: they stay linked to the player roles so the items keep working.
DELETE FROM `rbac_linked_permissions` WHERE `linkedId` BETWEEN 100550 AND 100599;
DELETE FROM `rbac_permissions` WHERE `id` BETWEEN 100550 AND 100599;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(100550, 'Command: temucraft expedition leave'),
(100551, 'Command: temucraft expedition status'),
(100552, 'Command: temucraft expedition powers'),
(100553, 'Command: temucraft expedition relics'),
(100554, 'Command: temucraft expedition ledger');
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(192, 100550),
(1014, 100550),
(192, 100551),
(1014, 100551),
(192, 100552),
(1014, 100552),
(192, 100553),
(1014, 100553),
(192, 100554),
(1014, 100554);
