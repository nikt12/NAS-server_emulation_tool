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

//реализация функции создания и связывания сокета
//аргументы:
//port - порт, с которым связывается сервер
//transport - протокол, по которому будет работать сервер
//qlen - длина очереди на подключение к сокету
int createServerSocket(const char *port, const char *transport, const char *qlen) {
	struct sockaddr_in sin;			//структура IP-адреса
	int s, type, proto, q_len, optval = 1;				//дескриптор и тип сокета

	q_len = atoi(qlen);
	//обнуляем структуру адреса
	memset(&sin, 0, sizeof(sin));
	//указываем тип адреса
	sin.sin_family = AF_INET;
	//указываем в качестве адреса шаблон INADDR_ANY, т.е. все сетевые интерфейсы
	sin.sin_addr.s_addr = INADDR_ANY;
	//конвертируем номер порта из пользовательского порядка байт в сетевой
	sin.sin_port = htons((unsigned short)atoi(port));

	//используем имя протокола для определения типа сокета
	if(strcmp(transport, "udp") == 0) {
		type = SOCK_DGRAM;
		proto = IPPROTO_UDP;
	}
	else {
		type = SOCK_STREAM;
		proto = IPPROTO_TCP;
	}

	//вызываем функцию создания сокета
	s = socket(PF_INET, type, proto);

	//переводим созданный сокет в неблокирующий режим для корректной работы epoll
	fdSetBlocking(s, 0);

	//проверяем правильность создания
	if (s < 0) {
		printf("Ошибка создания сокета: %s.\n", strerror(errno));
		return -1;
	}

	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	//привязка сокета с проверкой результата
	if(bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		printf("Ошибка связывания сокета: %s.\n", strerror(errno));
		return -1;
	}

	if(type == SOCK_STREAM)
		//запуск прослушивания с проверкой результата
		if (listen(s, q_len) < 0) {
			printf("Ошибка перевода сокета в режим прослушивания: %s.\n", strerror(errno));
			return -1;
		}
	return s;
}

void acceptNewConnection(int listeningSocket, connection *connList, int epollFD, struct epoll_event *event) {
	while(1) {
		int clientSocket;
		char clientName[32];
		struct sockaddr clientAddr;
		socklen_t clientAddrSize = sizeof(clientAddr);

		memset(&clientName, 0, sizeof(clientName));
		memset(&clientAddr, 0, sizeof(clientAddr));

		//принимаем подключение и проверяем результат
		clientSocket = accept(listeningSocket, &clientAddr, &clientAddrSize);
		if (clientSocket == -1) {
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
				break;
			else {
				perror("accept");
				break;
			}
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
						printf("%s\n", connStructOverflowNotification);
						write(clientSocket, connStructOverflowNotification, strlen(connStructOverflowNotification));
					}
			}
			memset(&clientName, 0, sizeof(clientName));
		}
	}
}

int identifySenderTCP(connection *connList, struct epoll_event *evListItem) {
	int i, n = -1;
	for (i = 0; i < NUM_OF_CONNECTIONS; i++)
		if (evListItem->data.fd == connList[i].clientSockFD)
			n = i;
	return n;
}

int identifySenderUDP(connection *connList, char *buffer) {
	int i, k, n = -1;
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
	char CRCmessage[BUFFERSIZE];
	memset(&CRCmessage, 0, sizeof(CRCmessage));

	//подсчитываем контрольную сумму текста присланного нам сообщения и помещаем ее в crcServerResult
	strncpy(CRCmessage, buffer, strlen(buffer) - 8);
	unsigned int CRC = crcSlow((unsigned char *)CRCmessage, strlen(CRCmessage));
	sprintf(crcServerResult, "%X", CRC);

	if (strcmp(connListItem->messageCRC32, crcServerResult) == 0)
		return 0;
	else {
		write(evListItem->data.fd, crcMissmatchNotification, strlen(crcMissmatchNotification));
		return -1;
	}
}

void firstServiceTCP(connection *connListItem, struct epoll_event *evListItem) {
	strcat(connListItem->messageText, firstSrvResponse);
	write(evListItem->data.fd, connListItem->messageText, strlen(connListItem->messageText));
}

void firstServiceUDP(int serverSock, connection *connListItem, struct sockaddr *clientAddr, socklen_t clientAddrSize) {
	strcat(connListItem->messageText, firstSrvResponse);
	int result = sendto(serverSock, connListItem->messageText, strlen(connListItem->messageText), 0, clientAddr, clientAddrSize);
	if(result == -1)
		perror("send");
}

void secondServiceTCP(connection *connListItem, struct epoll_event *evListItem) {
	strcat(connListItem->messageText, secondSrvResponse);
	write(evListItem->data.fd, connListItem->messageText, strlen(connListItem->messageText));
}

void secondServiceUDP(int serverSock, connection *connListItem, struct sockaddr *clientAddr, socklen_t clientAddrSize) {
	strcat(connListItem->messageText, secondSrvResponse);
	sendto(serverSock, connListItem->messageText, strlen(connListItem->messageText), 0, clientAddr, clientAddrSize);
}

void wrongServiceRequestedTCP(connection *connListItem, struct epoll_event *evListItem) {
	write(evListItem->data.fd, wrongSrvNotification, strlen(wrongSrvNotification)+1);
	printf("%s (%s) requested a non-existent service.\n", connListItem->clientNickName, connListItem->clientHostName);
	close(evListItem->data.fd);
}

void wrongServiceRequestedUDP(int serverSock, connection *connListItem, struct sockaddr *clientAddr, socklen_t clientAddrSize) {
	sendto(serverSock, wrongSrvNotification, strlen(wrongSrvNotification)+1, 0, clientAddr, clientAddrSize);
	printf("%s (%s) requested a non-existent service.\n", connListItem->clientNickName, connListItem->clientHostName);
}

void dataExchangeTCP(connection *connList, struct epoll_event *evListItem) {
	while(1) {
		int n = identifySenderTCP(connList, evListItem);

		char buffer[BUFFERSIZE];
		char crcServerResult[CRC32SIZE];

		memset(&buffer, 0, sizeof(buffer));
		memset(&crcServerResult, 0, sizeof(crcServerResult));

		//читаем полученные данные из сокета
		int result = read(evListItem->data.fd, buffer, sizeof(buffer));
		if (result == -1) {
			if(errno != EAGAIN)
				perror("read");
			break;
		}
		else if (result == 0) {
			//похоже, что клиент отключился
			printf("Client \"%s\" (at %s) has closed the connection.\n", connList[n].clientNickName, connList[n].clientHostName);
			close(evListItem->data.fd);						//исключаем связанный с ним дескриптор из epoll-инстанс
			memset(&connList[n], 0, sizeof(connList[n]));	//удаляем информацию об этом клиенте
			break;
		}

		result = readingInParts(&connList[n], buffer);
		if(result == 0)
			break;

		deSerializer(&connList[n], buffer);					//разбираем строку по полям экземпляра массива структур

		result = serverChecksumCalculateAndCompare(&connList[n], evListItem, buffer, crcServerResult);

		//сравниваем присланный клиентом CRC-32 с подсчитанным нами CRC-32
		if (result == 0) {
			connList[n].timeout = time(NULL);
			if(strcmp(connList[n].serviceName, firstServiceName) == 0) {
				//в случае совпадения, выводим текст сообщения на печать и отправляем его обратно клиенту
				printf("%s: %s\n", connList[n].clientNickName, connList[n].messageText);
				firstServiceTCP(&connList[n], evListItem);
			}
			else if(strcmp(connList[n].serviceName, secondServiceName) == 0) {
				//в случае совпадения, выводим текст сообщения на печать и отправляем его обратно клиенту
				printf("%s: %s\n", connList[n].clientNickName, connList[n].messageText);
				secondServiceTCP(&connList[n], evListItem);
			}
			else {
				wrongServiceRequestedTCP(&connList[n], evListItem);
				memset(&connList[n], 0, sizeof(connList[n]));
				memset(&buffer, 0, sizeof(buffer));
				memset(&crcServerResult, 0, strlen(crcServerResult)+1);
				break;
			}
		}
		else {
			perror("CRC"); //here must be write()
		}

		memset(&connList[n].messageText, 0, sizeof(connList[n].messageText));
		memset(&connList[n].messageCRC32, 0, sizeof(connList[n].messageCRC32));
		memset(&connList[n].length, 0, sizeof(connList[n].length));
		memset(&buffer, 0, sizeof(buffer));
		memset(&crcServerResult, 0, sizeof(crcServerResult));
	}
}

void dataExchangeUDP(int serverSock, connection *connList, struct epoll_event *evListItem) { //разобраться, что лучше использовать: evListItem or serverSocket
	while(1) {
		int n = -1;

		struct sockaddr clientAddr;						//структура IP-адреса клиента
		socklen_t clientAddrSize = sizeof(clientAddr);		//размер структуры адреса клиента

		char buffer[BUFFERSIZE];							//буфер, принимающий сообщения от клиента
		char crcServerResult[CRC32SIZE];					//буфер, в который помещается результат вычисления CRC-32
		char clientName[32];							//буфер для хранения хостнейма подключившегося клиента

		memset(&buffer, 0, sizeof(buffer));
		memset(&crcServerResult, 0, sizeof(crcServerResult));
		memset(&clientAddr, 0, sizeof(clientAddr));
		memset(&clientName, 0, sizeof(clientName));

		int result = recvfrom(serverSock, buffer, sizeof(buffer), 0, &clientAddr, &clientAddrSize);
		getnameinfo(&clientAddr, clientAddrSize, clientName, sizeof(clientName), NULL, 0, 0);
		if (result == -1) {
			if(errno != EAGAIN)
				perror("read");
			break;
		}

		//если от клиента получено предупреждающее сообщение о том, что будут присланы сегменты строки
		if(strncmp(buffer, segmentationWarning, strlen(segmentationWarning)) == 0) {
			Assembler(serverSock, buffer, &clientAddr, clientAddrSize);					//вызываем функцию сборщика сегментов
		}

		n = identifySenderUDP(connList, buffer);

		result = serverChecksumCalculateAndCompare(&connList[n], evListItem, buffer, crcServerResult);

		//сравниваем присланный клиентом CRC-32 с подсчитанным нами CRC-32
		if (result == 0) {
			if(strcmp(connList[n].serviceName, firstServiceName) == 0) {
				//в случае совпадения, выводим текст сообщения на печать и отправляем его обратно клиенту
				printf("%s: %s\n", connList[n].clientNickName, connList[n].messageText);
				firstServiceUDP(serverSock, &connList[n], &clientAddr, clientAddrSize);
			}
			else if(strcmp(connList[n].serviceName, secondServiceName) == 0) {
				//в случае совпадения, выводим текст сообщения на печать и отправляем его обратно клиенту
				printf("%s: %s\n", connList[n].clientNickName, connList[n].messageText);
				secondServiceUDP(serverSock, &connList[n], &clientAddr, clientAddrSize);
			}
			else {
				wrongServiceRequestedUDP(serverSock, &connList[n], &clientAddr, clientAddrSize);
				memset(&connList[n], 0, sizeof(connList[n]));
				memset(&buffer, 0, sizeof(buffer));
				memset(&crcServerResult, 0, strlen(crcServerResult)+1);
				break;
			}
		}
		else {
			//если контрольные суммы не совпали, уведомляем об этом клиента
			sendto(serverSock, crcMissmatchNotification, strlen(crcMissmatchNotification)+1, 0, &clientAddr, sizeof(clientAddr));
			perror("CRC"); //include response to client into perror() func
		}

		memset(&connList[n].messageText, 0, sizeof(connList[n].messageText));
		memset(&connList[n].messageCRC32, 0, sizeof(connList[n].messageCRC32));
		memset(&connList[n].length, 0, sizeof(connList[n].length));
		memset(&buffer, 0, sizeof(buffer));
		memset(&crcServerResult, 0, sizeof(crcServerResult));
	}
}
