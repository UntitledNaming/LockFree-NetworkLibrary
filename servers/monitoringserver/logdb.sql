CREATE DATABASE `logdb`;


CREATE TABLE `logdb`.`monitorlog` 
(
  `no` BIGINT NOT NULL AUTO_INCREMENT,
  `logtime`		DATETIME,
  `serverno`	INT NOT NULL,  -- 서버마다의 고유번호 각자 지정
  `type`		INT NOT NULL,
  -- avr  추가
  -- min  추가
  -- max  추가
  
PRIMARY KEY (`no`) );


insert into `monitorlog_%s` VALUES (.... );


monitorlog_202203
monitorlog_202204


