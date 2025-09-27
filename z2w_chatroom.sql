DROP DATABASE IF EXISTS `z2w_chatroom`;
CREATE DATABASE IF NOT EXISTS z2w_chatroom;
USE z2w_chatroom;
CREATE TABLE IF NOT EXISTS `users` (
    `id` BIGINT PRIMARY KEY AUTO_INCREMENT,
    `username` VARCHAR(100) NOT NULL,
    `email` VARCHAR(100) NOT NULL,
    `password_hash` TEXT NOT NULL,
    `salt` TEXT NOT NULL,
    `create_time` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (`username`),
    UNIQUE (`email`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;



