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

void strToLower(char *str) {
	int i;
	for(i = 0; str[i]; i++){
	  str[i] = tolower(str[i]);
	}
}

int argCheck(char *port, char *transport) {
	strToLower(transport);
	if((atoi(port) < 1024) || (atoi(port) > 65535))
		return -1;
	if((strcmp(transport, "udp") != 0) && (strcmp(transport, "tcp") != 0))
		return -2;
	return 0;
}

//реализация функции установки блокирующего/неблокирующего режима работы с файловым дескриптором
//аргументы:
//fd - файловый дескриптор сокета
//blocking - режим работы (0 - неблокирующий, 1 - блокирующий)
int fdSetBlocking(int fd, int blocking) {
    //сохраняем текущие флаги
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return 0;

    //устанавливаем режим работы
    if (blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags) != -1;
}

//реализация функции создания сокета и подключения к хосту
//аргументы:
//address - адрес хоста
//port - порт хоста
//transport - имя транспортного протокола
int createClientSocket(const char *address, const char *port, const char *transport) {
	int sockFD;								//файловый дескриптор сокета
	int portNum;							//номер порта в целочисленном формате
	int type, proto;						//тип транспортного протокола

	struct sockaddr_in serverAddr;			//структура, содержащая информацию об адресе

	memset(&serverAddr, 0, sizeof(serverAddr));

	//используем имя протокола для определения типа сокета
	if(strcmp(transport, "udp") == 0) {
		type = SOCK_DGRAM;
		proto = IPPROTO_UDP;
	}
	else if(strcmp(transport, "tcp") == 0) {
		type = SOCK_STREAM;
		proto = IPPROTO_TCP;
	}

	//вызываем функцию создания сокета с проверкой результата
	sockFD = socket(PF_INET, type, proto);
	if (sockFD < 0) {
		printf("Ошибка создания сокета: %s.\n", strerror(errno));
		return -1;
	}

	portNum = atoi(port);					//преобразовываем номер порта из строкового формата в целочисленный
	serverAddr.sin_port = htons(portNum);	//конвертируем номер порта из пользовательского порядка байт в сетевой
	serverAddr.sin_family = AF_INET;		//указываем тип адреса

	//конвертируем адрес в бинарный формат
	inet_pton(AF_INET, address, &serverAddr.sin_addr);

	if(type == SOCK_STREAM) {
		//вызываем функцию подключения к хосту с проверкой результата
		if(connect(sockFD, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) {
			printf("Ошибка подключения к %s: %s (%s)!\n", address, port, strerror(errno));
			return -1;
		}
		else
			printf("Successfully connected to %s:%s!\n\n", address, port);
	}
	return sockFD;							//возвращаем файловый дескриптор созданного сокета
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
    	memset(&cutBuffer, 0, sizeof(cutBuffer));
    }
}

int getMessageText(connection *conn) {
	char tempBuffer[MSGSIZE];

	memset(&tempBuffer, 0, sizeof(tempBuffer));

	printf("Type the message text: ");
	fgets(tempBuffer, sizeof(tempBuffer), stdin);
	strncpy(conn->messageText, tempBuffer, strlen(tempBuffer)-1);
	if (strcmp(conn->messageText, exitpr) == 0) {
		printf("Client is closing...\n\n");
		return -1;
	}
	return 0;
}

int sendMessageToServerTCP(int sockFD, connection *conn) {
	int result;
	char buffer[BUFFERSIZE];

	memset(&buffer, 0, sizeof(buffer));

	Serializer(conn, buffer);

	result = write(sockFD, buffer, strlen(buffer));
	if (result == -1) {
		perror("send");
		return -1;
	}
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
	if (result == -1) {
		perror("read");
		return -1;
	}
	else {
		if(strncmp(connStructOverflowNotification, buffer, strlen(buffer)+1) == 0) {
			printf("Client is closing...\n\n");
			return -2;
		}

		if(strcmp(wrongSrvNotification, buffer) == 0)
			return -2;

		printf("Server: %s\n\n", buffer);
		memset(&conn->messageText, 0, sizeof(conn->messageText));
		return result;
	}
}

int sendMessageToServerUDP(int sockFD, connection *conn, struct sockaddr_in *serverAddr, socklen_t serverAddrSize) {
	int result, size;
	char buffer[BUFFERSIZE];

	memset(&buffer, 0, sizeof(buffer));

	Serializer(conn, buffer);

	size = (int)strlen(buffer);
	if(size > (int)MTU) {
		result = Divider(sockFD, buffer, serverAddr, serverAddrSize);
		if(result > 0) {
			printf("Sent message: %s\n", conn->messageText);
			return result;
		}
		else {
			perror("send by parts");
			return -1;
		}
	}
	else {
		result = sendto(sockFD, buffer, strlen(buffer), 0, (struct sockaddr *)serverAddr, serverAddrSize);
		if (result == -1) {
			perror("send");
			return -1;
		}
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
	if (result == -1) {
		perror("read");
		return -1;
	}
	else {
		if(strncmp(connStructOverflowNotification, buffer, strlen(buffer)+1) == 0) {
			printf("Client is closing...\n\n");
			return -2;
		}

		if(strcmp(wrongSrvNotification, buffer) == 0)
			return -2;

		printf("Server: %s\n\n", buffer);
		int s = sizeof(conn->messageText);
		memset(&conn->messageText, 0, s);
		return result;
	}
}
