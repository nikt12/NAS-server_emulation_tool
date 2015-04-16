/*
 * clientFunctions.c
 *
 *  Created on: Apr 11, 2015
 *      Author: keinsword
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <math.h>
#include <fcntl.h>
#include "crc.h"
#include "protocol.h"

const char exitpr[] = "exitpr";

error errTable[20];

void errTableInit() {
	errTable[0].errCode = -1;
	errTable[0].errDesc = "Incorrect program arguments!\n";
	errTable[1].errCode = -2;
	errTable[1].errDesc = "Error creating the socket!\n";
	errTable[2].errCode = -3;
	errTable[2].errDesc = "Can't connect to server!\n";
	errTable[3].errCode = -4;
	errTable[3].errDesc = "Closing program...\n\n";
	errTable[4].errCode = -5;
	errTable[4].errDesc = "Error switching socket FD to blocking mode!\n";
	errTable[5].errCode = -6;
	errTable[5].errDesc = "There's no more place on the server!\n";
	errTable[5].errCode = -7;
	errTable[5].errDesc = "Server is offline. Closing program...\n";
	errTable[6].errCode = -8;
	errTable[6].errDesc = "You have requested wrong name of the service!\n";
	errTable[7].errCode = -9;
	errTable[7].errDesc = "Error sending message to the server!\n";
	errTable[8].errCode = -10;
	errTable[8].errDesc = "Error reading new message from the server!\n";
	errTable[9].errCode = -11;
	errTable[9].errDesc = "Error sending message by parts!\n";
}

//реализация функции создания сокета и подключения к хосту
//аргументы:
//address - адрес хоста
//port - порт хоста
//transport - имя транспортного протокола
int createClientSocket(const char *address, const char *port, const char *transport) {
	int sockFD;
	int portNum;
	int type, proto;
	struct sockaddr_in serverAddr;

	memset(&serverAddr, 0, sizeof(serverAddr));

	if(strcmp(transport, "udp") == 0) {
		type = SOCK_DGRAM;
		proto = IPPROTO_UDP;
	}
	else if(strcmp(transport, "tcp") == 0) {
		type = SOCK_STREAM;
		proto = IPPROTO_TCP;
	}

	sockFD = socket(PF_INET, type, proto);
	if (sockFD < 0)
		return -2;

	portNum = atoi(port);
	serverAddr.sin_port = htons(portNum);
	serverAddr.sin_family = AF_INET;

	inet_pton(AF_INET, address, &serverAddr.sin_addr);

	if(type == SOCK_STREAM) {
		if(connect(sockFD, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0)
			return -3;
		else
			printf("Successfully connected to %s:%s!\n\n", address, port);
	}
	else
		printf("Client was started. Using UDP protocol.\n\n");
	return sockFD;
}

void getClientInfo(connection *conn) {
	char tempBuffer[MSGSIZE];
	char cutBuffer[MSGSIZE];

	memset(&tempBuffer, 0, sizeof(tempBuffer));
	memset(&cutBuffer, 0, sizeof(cutBuffer));

    //цикл получения никнейма и имени сервиса
    while(1) {
    	printf("Type your nickname (from 4 to 15 symbols): ");
    	fgets(tempBuffer, sizeof(tempBuffer), stdin);
    	//избавляемся от \n, который fgets помещает в конец строки
    	strncpy(cutBuffer, tempBuffer, strlen(tempBuffer)-1);
    	memset(&tempBuffer, 0, sizeof(tempBuffer));
    	//ник не должен быть короче 4 и длиннее 15 символов
    	if((strlen(cutBuffer) >= 4) && (strlen(cutBuffer) <=15)) {
    		//сохраняем ник
    		strncpy(conn->clientNickName, cutBuffer, strlen(cutBuffer));
    		memset(&cutBuffer, 0, sizeof(cutBuffer));
			printf("Type the name of the service: ");
			fgets(tempBuffer, sizeof(tempBuffer), stdin);
			//избавляемся от \n, который fgets помещает в конец строки
			strncpy(cutBuffer, tempBuffer, strlen(tempBuffer)-1);
			memset(&tempBuffer, 0, sizeof(tempBuffer));
			//сохраняем имя сервиса
			strncpy(conn->serviceName, cutBuffer, strlen(cutBuffer));
			memset(&cutBuffer, 0, sizeof(cutBuffer));
			break;
    	}
    	else
    		printf("Wrong nickname! Try again.\n");
    	memset(&cutBuffer, 0, sizeof(cutBuffer));
    }
}

int getMessageText(connection *conn) {
	char tempBuffer[MSGSIZE];

	memset(&tempBuffer, 0, sizeof(tempBuffer));

	printf("Type the message text: ");
	fgets(tempBuffer, sizeof(tempBuffer), stdin);
	strncpy(conn->messageText, tempBuffer, strlen(tempBuffer)-1);
	if (strcmp(conn->messageText, exitpr) == 0)
		return -4;
	return 0;
}

int sendMessageToServerTCP(int sockFD, connection *conn) {
	int result;
	char buffer[BUFFERSIZE];
	//char buffer1[BUFFERSIZE]; int n;

	memset(&buffer, 0, sizeof(buffer));
	//memset(&buffer1, 0, sizeof(buffer1));

	Serializer(conn, buffer);

/*	strncpy(buffer1, buffer, 25);
	n = write(sockFD, buffer1, strlen(buffer1));
	memset(&buffer1, 0, sizeof(buffer1));
	int i, j;
	for(i = 25, j = 0; i < 30; i++, j++)
		buffer1[j]=buffer[i];
	sleep(5);
	n = write(sockFD, buffer1, strlen(buffer1));
	memset(&buffer1, 0, sizeof(buffer1));
	for(i = 30, j = 0; i < strlen(buffer); i++, j++)
		buffer1[j]=buffer[i];
	sleep(5);
	n = write(sockFD, buffer1, strlen(buffer1));
	memset(&buffer1, 0, sizeof(buffer1));*/

	result = write(sockFD, buffer, strlen(buffer));
	if (result == -1)
		return -9;
	else {
		printf("Sent message: %s\n", conn->messageText);
		return result;
	}
}

int recvMessageFromServerTCP(int sockFD, connection *conn) {
	int result;
	char buffer[BUFFERSIZE];

	memset(&buffer, 0, sizeof(buffer));

	result = read(sockFD, buffer, sizeof(buffer));
	if (result == -1)
		return -10;
	else {
		if(strncmp(connStructOverflowNotification, buffer, strlen(buffer)+1) == 0)
			return -6;

		if(strcmp(wrongSrvNotification, buffer) == 0)
			return -8;

		if(strcmp(srvIsOffline, buffer) == 0)
			return -7;

		deSerializer(conn, buffer);
		printf("Server: %s\n\n", conn->messageText);
		memset(&conn->messageText, 0, sizeof(conn->messageText));
		return result;
	}
}

int sendMessageToServerUDP(int sockFD, connection *conn, struct sockaddr_in *serverAddr, socklen_t serverAddrSize) {
	int result;
	size_t size;
	char buffer[BUFFERSIZE];

	memset(&buffer, 0, sizeof(buffer));

	Serializer(conn, buffer);

	size = strlen(buffer);
	if(size > MTU) {
		result = Divider(sockFD, buffer, serverAddr, serverAddrSize);
		if(result < 0)
			return result;
		else {
			printf("Sent message: %s\n", conn->messageText);
			return result;
		}
	}
	else {
		result = sendto(sockFD, buffer, strlen(buffer), 0, (struct sockaddr *)serverAddr, serverAddrSize);
		if (result == -1)
			return -9;
		else {
			printf("Sent message: %s\n", conn->messageText);
			return result;
		}
	}
}

int recvMessageFromServerUDP(int sockFD, connection *conn, struct sockaddr_in *serverAddr, socklen_t serverAddrSize) {
	int result;
	char buffer[BUFFERSIZE];

	memset(&buffer, 0, sizeof(buffer));

	result = recvfrom(sockFD, buffer, sizeof(buffer), 0, (struct sockaddr *)serverAddr, &serverAddrSize);
	if (result == -1)
		return -10;
	else {
		if(strncmp(connStructOverflowNotification, buffer, strlen(buffer)+1) == 0)
			return -6;

		if(strcmp(wrongSrvNotification, buffer) == 0)
			return -8;

		if(strcmp(srvIsOffline, buffer) == 0)
			return -7;

		deSerializer(conn, buffer);
		printf("Server: %s\n\n", conn->messageText);
		memset(&conn->messageText, 0, sizeof(conn->messageText));
	}
	return result;
}
