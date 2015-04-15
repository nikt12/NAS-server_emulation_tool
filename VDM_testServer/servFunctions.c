/*
 * servFunctions.c
 *
 *  Created on: Apr 4, 2015
 *      Author: keinsword
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include "crc.h"
#include "protocol.h"
#include "servFunctions.h"

int errno;

void errTableInit() {
}


void strToLower(char *str) {
	int i;
	for(i = 0; str[i]; i++){
	  str[i] = tolower(str[i]);
	}
}

int checkArgs(char *port, char *transport) {
	strToLower(transport);
	if((atoi(port) < 1024) || (atoi(port) > 65535) || ((strcmp(transport, "udp") != 0) && (strcmp(transport, "tcp") != 0))) {
		return -2;
	}
	return 0;
}

//реализация функции установки блокирующего/неблокирующего режима работы с файловым дескриптором
//аргументы:
//fd - файловый дескриптор сокета
//blocking - режим работы (0 - неблокирующий, 1 - блокирующий)
int fdSetBlocking(int fd, int blocking) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return 0;

    if (blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags) != -1;
}

//реализация функции создания и связывания сокета
//аргументы:
//port - порт, с которым связывается сервер
//transport - протокол, по которому будет работать сервер
//qlen - длина очереди на подключение к сокету
int createServerSocket(const char *port, const char *transport, const char *qlen) {
	struct sockaddr_in sin;			//структура IP-адреса
	int s, result, type, proto, q_len, optval = 1;				//дескриптор и тип сокета

	q_len = atoi(qlen);

	memset(&sin, 0, sizeof(sin));

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons((unsigned short)atoi(port));

	if(strcmp(transport, "udp") == 0) {
		type = SOCK_DGRAM;
		proto = IPPROTO_UDP;
	}
	else {
		type = SOCK_STREAM;
		proto = IPPROTO_TCP;
	}

	s = socket(PF_INET, type, proto);
	if (s < 0)
		handleErr("Socket", s);

	fdSetBlocking(s, 0);
	if (s < 0)
		handleErr("Socket", s);

	result = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	if(result < 0)
		handleErr("Set socket option", result);

	result = bind(s, (struct sockaddr *)&sin, sizeof(sin));
	if(result < 0)
		handleErr("Bind", result);

	if(type == SOCK_STREAM) {
		result = listen(s, q_len);
		if(result < 0)
			handleErr("Listen", result);
	}
	return s;
}

int acceptNewConnection(int listeningSocket, connection *connList, int epollFD, struct epoll_event *event) {
	int clientSocket;
	char clientName[32];
	struct sockaddr clientAddr;
	socklen_t clientAddrSize = sizeof(clientAddr);

	memset(&clientName, 0, sizeof(clientName));
	memset(&clientAddr, 0, sizeof(clientAddr));

	clientSocket = accept(listeningSocket, &clientAddr, &clientAddrSize);
	if (clientSocket < 0) {
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
			return 0;
		else
			return -6;
	}
	else {
		int j;
		for(j = 0; j < NUM_OF_CONNECTIONS; j++) {
			if(connList[j].clientHostName[0] == '\0') {
				fdSetBlocking(clientSocket, 0);
				connList[j].clientSockFD = clientSocket;
				getnameinfo(&clientAddr, clientAddrSize, clientName, sizeof(clientName), NULL, 0, 0);
				sprintf(connList[j].clientHostName, "%s", clientName);
				connList[j].timeout = time(NULL);
				event->data.fd = clientSocket;
				event->events = EPOLLIN;
				epoll_ctl(epollFD, EPOLL_CTL_ADD, clientSocket, event);
				printf("Accepted connection on descriptor %d (hostname: %s)!\n", clientSocket, clientName);
				break;
			}
			else
				if(j == NUM_OF_CONNECTIONS-1) {
					write(clientSocket, connStructOverflowNotification, strlen(connStructOverflowNotification));
					return -7;
				}
		}
		return clientSocket;
	}
}

int identifySenderTCP(connection *connList, struct epoll_event *evListItem) {
	int i, n = -8;
	for (i = 0; i < NUM_OF_CONNECTIONS; i++)
		if (evListItem->data.fd == connList[i].clientSockFD) {
			n = i;
			break;
		}
	return n;
}

int identifySenderUDP(connection *connList, char *buffer) {
	int i, k, n = -8;
	connection tempConn[1];
	memset(&tempConn, 0, sizeof(tempConn));
	deSerializer(&tempConn[0], buffer);
	for(i = 0; i < NUM_OF_CONNECTIONS; i++) {
		if(strcmp(connList[i].clientNickName, tempConn[0].clientNickName) == 0) {
			n = i;
			deSerializer(&connList[n], buffer);
			memset(&tempConn, 0, sizeof(tempConn));
			break;
		}
		else if(i == NUM_OF_CONNECTIONS - 1) {
			for(k = 0; k < NUM_OF_CONNECTIONS; k++)
				if(connList[k].clientNickName[0] == '\0') {
					n = k;
					break;
				}
			deSerializer(&connList[n], buffer);
			memset(&tempConn, 0, sizeof(tempConn));
		}
	}
	return n;
}

int readingInParts(connection *connListItem, char *buffer) {
	if(connListItem->segmentationFlag == 0) {
		isMessageEntire(connListItem, buffer);
		if(connListItem->segmentationFlag == 1) {
			Accumulator(connListItem, buffer);
			memset(&buffer, 0, sizeof(buffer));
			return 0;
		}
	}
	else {
		Accumulator(connListItem, buffer);
		if(connListItem->segmentationFlag == 1) {
			memset(&buffer, 0, sizeof(buffer));
			return 0;
		}
	}
	return 1;
}

int serverChecksumCalculateAndCompare(connection *connListItem, struct epoll_event *evListItem, char *buffer, char *crcServerResult) {
	unsigned int CRC;
	char CRCmessage[BUFFERSIZE];
	memset(&CRCmessage, 0, sizeof(CRCmessage));

	strncpy(CRCmessage, buffer, strlen(buffer) - 8);
	CRC = crcSlow((unsigned char *)CRCmessage, strlen(CRCmessage));
	sprintf(crcServerResult, "%X", CRC);

	if (strcmp(connListItem->messageCRC32, crcServerResult) == 0)
		return 0;
	else
		return -10;
}

int firstServiceTCP(connection *connListItem, struct epoll_event *evListItem, char *buffer) {
	int result;
	printf("%s: %s\n", connListItem->clientNickName, connListItem->messageText);
	strcat(connListItem->messageText, firstSrvResponse);
	Serializer(connListItem, buffer);
	result = write(evListItem->data.fd, buffer, strlen(buffer));
	if(result < 0)
		return -12;
	return result;
}

int firstServiceUDP(int serverSock, connection *connListItem, struct sockaddr *clientAddr, socklen_t clientAddrSize, char *buffer) {
	int result;
	printf("%s: %s\n", connListItem->clientNickName, connListItem->messageText);
	strcat(connListItem->messageText, firstSrvResponse);
	Serializer(connListItem, buffer);
	result = sendto(serverSock, buffer, strlen(buffer), 0, clientAddr, clientAddrSize);
	if(result < 0)
		return -12;
	return result;
}

int secondServiceTCP(connection *connListItem, struct epoll_event *evListItem, char *buffer) {
	int result;
	printf("%s: %s\n", connListItem->clientNickName, connListItem->messageText);
	strcat(connListItem->messageText, secondSrvResponse);
	Serializer(connListItem, buffer);
	result = write(evListItem->data.fd, buffer, strlen(buffer));
	if(result < 0)
		return -12;
	return result;
}

int secondServiceUDP(int serverSock, connection *connListItem, struct sockaddr *clientAddr, socklen_t clientAddrSize, char *buffer) {
	int result;
	printf("%s: %s\n", connListItem->clientNickName, connListItem->messageText);
	strcat(connListItem->messageText, secondSrvResponse);
	Serializer(connListItem, buffer);
	result = sendto(serverSock, buffer, strlen(buffer), 0, clientAddr, clientAddrSize);
	if(result < 0)
		return -12;
	return result;
}

int dataExchangeTCP(connection *connList, struct epoll_event *evListItem) {
	int n = identifySenderTCP(connList, evListItem);
	if(n < 0)
		return n;

	char buffer[BUFFERSIZE];
	char crcServerResult[CRC32SIZE];

	memset(&buffer, 0, sizeof(buffer));
	memset(&crcServerResult, 0, sizeof(crcServerResult));

	int result = read(evListItem->data.fd, buffer, sizeof(buffer));
	if (result == -1) {
		if(errno != EAGAIN)
			return -9;
		return 0;
	}
	if (result == 0) {
		printf("Client \"%s\" (at %s) has closed the connection.\n", connList[n].clientNickName, connList[n].clientHostName);
		close(evListItem->data.fd);
		memset(&connList[n], 0, sizeof(connList[n]));
		return 0;
	}

	result = readingInParts(&connList[n], buffer);		//fix reading in parts
	if(result == 0)
		return 0;

	deSerializer(&connList[n], buffer);

	result = serverChecksumCalculateAndCompare(&connList[n], evListItem, buffer, crcServerResult); //!!!!!!!!!!!

	memset(&buffer, 0, sizeof(buffer));

	if (result == 0) {
		connList[n].timeout = time(NULL);
		if(strcmp(connList[n].serviceName, firstServiceName) == 0) {
			result = firstServiceTCP(&connList[n], evListItem, buffer);
			if(result < 0)
				return result;
		}
		else if(strcmp(connList[n].serviceName, secondServiceName) == 0) {
			result = secondServiceTCP(&connList[n], evListItem, buffer);
			if(result < 0)
				return result;
		}
		else {
			write(evListItem->data.fd, wrongSrvNotification, strlen(wrongSrvNotification)+1);
			close(evListItem->data.fd);
			memset(&connList[n], 0, sizeof(connList[n]));
			return -11;
		}
	}
	else {
		write(evListItem->data.fd, crcMissmatchNotification, strlen(crcMissmatchNotification));
		return result;
	}

	memset(&connList[n].messageText, 0, sizeof(connList[n].messageText));
	memset(&connList[n].messageCRC32, 0, sizeof(connList[n].messageCRC32));
	memset(&connList[n].length, 0, sizeof(connList[n].length));
	return 0;
}

int dataExchangeUDP(int serverSock, connection *connList, struct epoll_event *evListItem) { //разобраться, что лучше использовать: evListItem or serverSocket
	int n = -1, result;
	struct sockaddr clientAddr;
	socklen_t clientAddrSize = sizeof(clientAddr);
	char buffer[BUFFERSIZE];
	char crcServerResult[CRC32SIZE];
	char clientName[32];

	memset(&buffer, 0, sizeof(buffer));
	memset(&crcServerResult, 0, sizeof(crcServerResult));
	memset(&clientAddr, 0, sizeof(clientAddr));
	memset(&clientName, 0, sizeof(clientName));

	result = recvfrom(serverSock, buffer, sizeof(buffer), 0, &clientAddr, &clientAddrSize);
	getnameinfo(&clientAddr, clientAddrSize, clientName, sizeof(clientName), NULL, 0, 0);
	if (result == -1) {
		if(errno != EAGAIN)
			return -9;
		return 0;
	}

	if(strncmp(buffer, segmentationWarning, strlen(segmentationWarning)) == 0) {
		Assembler(serverSock, buffer, &clientAddr, clientAddrSize);
	}

	n = identifySenderUDP(connList, buffer);
	if(n < 0)
		return n;

	result = serverChecksumCalculateAndCompare(&connList[n], evListItem, buffer, crcServerResult);

	memset(&buffer, 0, sizeof(buffer));

	if (result == 0) {
		if(strcmp(connList[n].serviceName, firstServiceName) == 0) {
			result = firstServiceUDP(serverSock, &connList[n], &clientAddr, clientAddrSize, buffer);
			if(result < 0)
				return result;
		}
		else if(strcmp(connList[n].serviceName, secondServiceName) == 0) {
			result = secondServiceUDP(serverSock, &connList[n], &clientAddr, clientAddrSize, buffer);
			if(result < 0)
				return result;
		}
		else {
			sendto(serverSock, wrongSrvNotification, strlen(wrongSrvNotification)+1, 0, &clientAddr, clientAddrSize);
			memset(&connList[n], 0, sizeof(connList[n]));
			return -11;
		}
	}
	else {
		sendto(serverSock, crcMissmatchNotification, strlen(crcMissmatchNotification)+1, 0, &clientAddr, clientAddrSize);
		return result;
	}

	memset(&connList[n].messageText, 0, sizeof(connList[n].messageText));
	memset(&connList[n].messageCRC32, 0, sizeof(connList[n].messageCRC32));
	memset(&connList[n].length, 0, sizeof(connList[n].length));
	return 0;
}
