-- mod-temucraft-expeditions: its player commands (all under .temucraft) as RBAC permissions 100550-100599. Linked to the stock player
-- role 195 and the Temucraft player role 1010, so every account has them by default. To take one away, remove its
-- link here (or deny it per account with .rbac account deny <account> <id>); to give it to staff only, link it to
-- 1011-1014 instead.
DELETE FROM `rbac_linked_permissions` WHERE `linkedId` BETWEEN 100550 AND 100599;
DELETE FROM `rbac_permissions` WHERE `id` BETWEEN 100550 AND 100599;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(100550, 'Command: temucraft expedition leave'),
(100551, 'Command: temucraft expedition status'),
(100552, 'Command: temucraft expedition powers'),
(100553, 'Command: temucraft expedition relics'),
(100554, 'Command: temucraft expedition ledger');
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(195, 100550),
(1010, 100550),
(195, 100551),
(1010, 100551),
(195, 100552),
(1010, 100552),
(195, 100553),
(1010, 100553),
(195, 100554),
(1010, 100554);
