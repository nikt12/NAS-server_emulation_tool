/*
 * servFunctions.h
 *
 *  Created on: Apr 4, 2015
 *      Author: keinsword
 */

#ifndef SERVFUNCTIONS_H_
#define SERVFUNCTIONS_H_

void strToLower(char *str);

int argCheck(char *port, char *transport);

int fdSetBlocking(int fd, int blocking);

int createServerSocket(const char *port, const char *transport, const char *qlen);

void acceptNewConnection(int listeningSocket, connection *connList, int epollFD, struct epoll_event *event);

int identifySenderTCP(connection *connList, struct epoll_event *evListItem);

int identifySenderUDP(connection *connList, char *buffer);

int readingInParts(connection *connListItem, char *buffer);

int serverChecksumCalculateAndCompare(connection *connListItem, struct epoll_event *evListItem, char *buffer, char *crcServerResult);

void firstServiceTCP(connection *connListItem, struct epoll_event *evListItem);

void firstServiceUDP(int serverSock, connection *connListItem, struct sockaddr *clientAddr, socklen_t clientAddrSize);

void secondServiceTCP(connection *connListItem, struct epoll_event *evListItem);

void secondServiceUDP(int serverSock, connection *connListItem, struct sockaddr *clientAddr, socklen_t clientAddrSize);

void wrongServiceRequestedTCP(connection *connListItem, struct epoll_event *evListItem);

void wrongServiceRequestedUDP(int serverSock, connection *connListItem, struct sockaddr *clientAddr, socklen_t clientAddrSize);

void dataExchangeTCP(connection *connList, struct epoll_event *evListItem);

void dataExchangeUDP(int serverSock, connection *connList, struct epoll_event *evListItem);

#endif /* SERVFUNCTIONS_H_ */
