CREATE TABLE `user` (
  `uid` int(11) NOT NULL AUTO_INCREMENT,
  `username` varchar(20) NOT NULL,
  `email` varchar(100) NOT NULL,
  `password` varchar(32) NOT NULL,
  `salt` varchar(8) NOT NULL,
  `registerTime` datetime NOT NULL,
  `loginTime` datetime DEFAULT NULL,
  `loginTimes` int(11) NOT NULL DEFAULT 0,
  PRIMARY KEY (`uid`),
  UNIQUE KEY `username` (`username`),
  UNIQUE KEY `email` (`email`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

INSERT INTO `user` VALUES (1,'admin','admin@yeah.net',md5(concat(md5('123456'), '12345678')),'12345678','2019-10-14 22:13:55','2021-08-14 10:04:23',9);

