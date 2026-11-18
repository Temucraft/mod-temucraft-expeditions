-- mod-temucraft-expeditions: its game master commands as RBAC permissions (100501-100507). Each is linked to the
-- stock security role of its old level (194 moderator, 193 game master, 192 administrator) and to the matching
-- Temucraft GM tier (1011 Triager, 1012 Entertainer, 1014 Administrator). Grant or deny them per account with
-- .rbac account grant / deny, or link them to other roles.
DELETE FROM `rbac_linked_permissions` WHERE `linkedId` BETWEEN 100501 AND 100599;
DELETE FROM `rbac_permissions` WHERE `id` BETWEEN 100501 AND 100599;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(100501, 'Command: expedition start'),
(100502, 'Command: expedition list'),
(100503, 'Command: expedition reload'),
(100504, 'Command: expedition here'),
(100505, 'Command: expedition spot'),
(100506, 'Command: expedition unspot'),
(100507, 'Command: expedition spots');
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(193, 100501),
(1012, 100501),
(193, 100502),
(1012, 100502),
(192, 100503),
(1014, 100503),
(193, 100504),
(1012, 100504),
(193, 100505),
(1012, 100505),
(193, 100506),
(1012, 100506),
(193, 100507),
(1012, 100507);
