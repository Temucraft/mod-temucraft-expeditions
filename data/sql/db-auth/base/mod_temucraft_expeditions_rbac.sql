-- mod-temucraft-expeditions: its game master commands as RBAC permissions (100501-100508). Each is linked to the
-- stock security role of its old level (194 moderator, 193 game master, 192 administrator) and to the matching
-- Temucraft GM tier (1011 Triager, 1012 Entertainer, 1014 Administrator). Grant or deny them per account with
-- .rbac account grant / deny, or link them to other roles.
DELETE FROM `rbac_linked_permissions` WHERE `linkedId` BETWEEN 100501 AND 100549;
DELETE FROM `rbac_permissions` WHERE `id` BETWEEN 100501 AND 100549;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(100501, 'Command: temucraft expedition start'),
(100502, 'Command: temucraft expedition list'),
(100503, 'Command: temucraft expedition reload'),
(100504, 'Command: temucraft expedition here'),
(100505, 'Command: temucraft expedition spot'),
(100506, 'Command: temucraft expedition unspot'),
(100507, 'Command: temucraft expedition spots'),
(100508, 'Command: temucraft expedition nemesis');
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(192, 100501),
(1014, 100501),
(192, 100502),
(1014, 100502),
(192, 100503),
(1014, 100503),
(192, 100504),
(1014, 100504),
(192, 100505),
(1014, 100505),
(192, 100506),
(1014, 100506),
(192, 100507),
(1014, 100507),
(192, 100508),
(1014, 100508);
