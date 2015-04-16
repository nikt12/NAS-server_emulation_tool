/*
 * clientFunctions.h
 *
 *  Created on: Apr 11, 2015
 *      Author: keinsword
 */

#ifndef CLIENTFUNCTIONS_H_
#define CLIENTFUNCTIONS_H_

extern const char exitpr[];

extern error errTable[20];

void errTableInit();

int createClientSocket(const char *address, const char *port, const char *transport);

void getClientInfo(connection *conn);

int getMessageText(connection *conn);

int sendMessageToServerTCP(int sockFD, connection *conn);

int recvMessageFromServerTCP(int sockFD, connection *conn);

int sendMessageToServerUDP(int sockFD, connection *conn, struct sockaddr_in *serverAddr, socklen_t serverAddrSize);

int recvMessageFromServerUDP(int sockFD, connection *conn, struct sockaddr_in *serverAddr, socklen_t serverAddrSize);

#endif /* CLIENTFUNCTIONS_H_ */
