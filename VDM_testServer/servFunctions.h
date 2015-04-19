/*
 * servFunctions.h
 *
 *  Created on: Apr 4, 2015
 *      Author: keinsword
 */

#ifndef SERVFUNCTIONS_H_
#define SERVFUNCTIONS_H_

extern error errTable[20];

extern int endEventLoop, endMainLoop;

void errTableInit();

void timeoutCheck(connection *connList, struct epoll_event *evList);

void strToLower(char *str);

int checkArgs(char *port, char *transport);

int fdSetBlocking(int fd, int blocking);

int createServerSocket(int port, const char *transport, int qlen);

int acceptNewConnection(int listeningSocket, connection *connList, int epollFD, struct epoll_event *event);

int identifySenderTCP(connection *connList, struct epoll_event *evListItem);

int identifySenderUDP(connection *connList, char *buffer);

int serverChecksumCalculateAndCompare(connection *connListItem, struct epoll_event *evListItem, char *buffer, char *crcServerResult);

int firstServiceTCP(connection *connListItem, struct epoll_event *evListItem, char *buffer);

int firstServiceUDP(int serverSock, connection *connListItem, struct sockaddr *clientAddr, socklen_t clientAddrSize, char *buffer);

int secondServiceTCP(connection *connListItem, struct epoll_event *evListItem, char *buffer);

int secondServiceUDP(int serverSock, connection *connListItem, struct sockaddr *clientAddr, socklen_t clientAddrSize, char *buffer);

int dataExchangeTCP(connection *connList, struct epoll_event *evListItem);

int dataExchangeUDP(int serverSock, connection *connList, struct epoll_event *evListItem);

void sig_handler(int signum);

int checkIpStack(config_t *cfg,const char *serverInterface, const char *serviceName);

#endif /* SERVFUNCTIONS_H_ */
