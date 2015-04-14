/*
 * protocol.c
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
#include "protocol.h"
#include "servFunctions.h"
#include "crc.h"

const char segmentationWarning[] = "SEGMENTATED_MESSAGES_WILL_COME:";	//формат предупреждающего сообщения
const char ACK[] = "ACK";					//формат сообщения-подтверждения

const char firstServiceName[] = "A";	//имя первого сервиса
const char secondServiceName[] = "B";	//имя второго сервиса

const char firstSrvResponse[] = " (response from service A)";
const char secondSrvResponse[] = " (response from service B)";

const char wrongSrvNotification[] = "You have requested a non-existent service.";
const char connStructOverflowNotification[] = "No more place for new clients. Please, try again later.";
const char crcMissmatchNotification[] = "Checksum missmatch.";

void timeoutCheck(connection *connList, struct epoll_event *evList) {
	time_t timeout;
	int j = 0;
	for(j = 0; j < NUM_OF_CONNECTIONS; j++) {
		timeout = time(NULL) - connList[j].timeout;
		if((timeout > TIMEOUT) && (connList[j].clientHostName[0] != '\0')) {
			printf("Timeout period for \"%s\" has experied.\n", connList[j].clientHostName);
			close(evList[j].data.fd);
			memset(&connList[j], 0, sizeof(connList[j]));
			break;
		}
	}
}

//реализация функции конвертирования данных структуры в строку для пересылки по сети
//аргументы:
//connection - структура данных о клиенте и параметрах соединения
//buffer - строка, в которую будет помещен результат
void Serializer(connection *connection, char *buffer) {
	char tempString[BUFFERSIZE];
	char messageLength[5];
	memset(&tempString, 0, sizeof(tempString));
	memset(&messageLength, 0, sizeof(messageLength));

	strcat(buffer, connection->protoName);
	strcat(buffer, "|");
	strcat(buffer, connection->protoVersion);
	strcat(buffer, "|");
	strcat(tempString, connection->clientNickName);
	strcat(tempString, "|");
	strcat(tempString, connection->serviceName);
	strcat(tempString, "|");
	strcat(tempString, connection->messageText);
	strcat(tempString, "|");
	sprintf(messageLength, "%d", (int)(strlen(buffer) + strlen(tempString) + 8 + 1));
	sprintf(connection->length, "%d", (int)(atoi(messageLength) + strlen(messageLength)));
	strcat(buffer, connection->length);
	strcat(buffer, "|");
	strcat(buffer, tempString);
	sprintf(connection->messageCRC32, "%X", (unsigned int)crcSlow((unsigned char *)buffer, strlen(buffer)));
	strcat(buffer, connection->messageCRC32);
}

//реализация функции разбора подстрок строки по полям структуры
//аргументы:
//connList - массив структур, описывающие соединения с клиентами
//текущий элемент массива структур
//буфер, в котором находится строка сообщения
void deSerializer(connection *connListItem, char *buffer) {
	int i = 0, j = 0;
	while(buffer[i] != 124) {
		connListItem->protoName[j] = buffer[i];
		i++;
		j++;
	}
	i++;
	j = 0;
	while(buffer[i] != 124) {
		connListItem->protoVersion[j] = buffer[i];
		i++;
		j++;
	}
	i++;
	j = 0;
	while(buffer[i] != 124) {
		connListItem->length[j] = buffer[i];
		i++;
		j++;
	}
	i++;
	j = 0;
	while(buffer[i] != 124) {
		connListItem->clientNickName[j] = buffer[i];
		i++;
		j++;
	}
	i++;
	j = 0;
	while(buffer[i] != 124) {
		connListItem->serviceName[j] = buffer[i];
		i++;
		j++;
	}
	i++;
	j = 0;
	while(buffer[i] != 124) {
		connListItem->messageText[j] = buffer[i];
		i++;
		j++;
	}
	i++;
	j = 0;
	while(buffer[i] != 0) {
		connListItem->messageCRC32[j] = buffer[i];
		i++;
		j++;
	}
}

//реализация функции сегментирования строки
//аргументы:
//sockFD - файловый дескриптор сокета
//buffer - исходная строка
int Divider(int sockFD, char *buffer, struct sockaddr_in *serverAddr, socklen_t serverAddrSize) {
	int segNum;								//число сегментов, на которые будет поделена строка
	int i, j, k;							//счетчики циклов
	int done = 0;							//счетчик отправленных сегментов

	char segWarning[25];					//сообщение, предупреждающее сервер о том, что будет отправлена последовательность сегментов
	char tempBuffer[5];						//временный буфер

	char segArray[NUM_OF_SEGMENTS][MTU+1];	//массив строк-сегментов исходной строки

	//инициализируем нулями используемые строки и массив строк
	memset(&segWarning, 0, sizeof(segWarning));
	memset(&tempBuffer, 0, sizeof(tempBuffer));
	memset(&segArray, 0, sizeof(segArray));


	//проверяем, во сколько раз строка больше, чем MTU
	if((strlen(buffer) % MTU) == 0)
		segNum = strlen(buffer) / MTU;
	else
		segNum = strlen(buffer) / MTU + 1;

	//формируем предупреждающее сообщение
	sprintf(segWarning, "%s%d", segmentationWarning, segNum);

	//для корректного обмена переводим сокет в блокирующий режим
	fdSetBlocking(sockFD, 1);

	//отправляем предупреждающее сообщение
	sendto(sockFD, segWarning, strlen(segWarning), 0, serverAddr, serverAddrSize);

	//ждем подтверждения от сервера
	if((recvfrom(sockFD, tempBuffer, sizeof(tempBuffer), 0, serverAddr, &serverAddrSize) > 0) && (strcmp(tempBuffer, ACK) == 0)) {
		memset(&tempBuffer, 0, sizeof(tempBuffer));
		//цикл сегментирования исходной строки
		for(j = 0; j < segNum; j++)
			for(i = (j*strlen(buffer))/segNum, k = 0; i < ((j+1)*strlen(buffer))/segNum; i++, k++)
				segArray[j][k] = buffer[i];
		//цикл обмена данными с сервером
		for(j = 0; j < segNum; j++) {
			//отправляем очередной сегмент
			sendto(sockFD, segArray[j], strlen(segArray[j]), 0, serverAddr, serverAddrSize);
			//ждем подтверждения
			if((recvfrom(sockFD, tempBuffer, sizeof(tempBuffer), 0, serverAddr, &serverAddrSize) > 0) && (strcmp(tempBuffer, ACK) == 0)) {
				memset(&tempBuffer, 0, sizeof(tempBuffer));
				//инкрементируем счетчик отправленных сегментов в случае успеха
				done++;
			}
		}
	}

	//возвращаем сокет в неблокирующий режим
	fdSetBlocking(sockFD, 0);

	memset(&buffer, 0, sizeof(buffer));

	//в случае, если счетчик отправленных сегментов равен предварительно рассчитанному числу сегментов, возвращаем число сегментов
	if(done == segNum)
		return segNum;
	else
		return -1;
}

//реализация функции сбора сегментов строки в единую строку
//аргументы:
//evlist[] - массив структур, описывающих события epoll
//currItem - текущий элемент этого массива
//buffer - буфер, в который будет записана собранная строка
void Assembler(int sockFD, char *buffer, struct sockaddr *clientAddr, socklen_t clientAddrSize) {
	int i, j;						//счетчики циклов

	char strSegNum[5];				//число сегментов в строковом формате
	char accumBuffer[BUFFERSIZE];	//накопительный буфер
	char tempBuffer[MTU+1];			//временный буфер

	//инициализируем нулями строки, которые будем использовать далее
	memset(&strSegNum, 0, sizeof(strSegNum));
	memset(&accumBuffer, 0, sizeof(accumBuffer));
	memset(&tempBuffer, 0, sizeof(tempBuffer));

	//считываем число сегментов в строковом формате из конца полученного предупреждающего сообщения
	for(i = 0, j = strlen(segmentationWarning); j < strlen(buffer); i++, j++)
		strSegNum[i] = buffer[j];
	int segNum = atoi(strSegNum);

	//устанавливаем блокирующий режим для корректного обмена данными
	fdSetBlocking(sockFD, 1);

	//отправляем клиенту подтверждение того, что нами получено и обработано предупреждающее сообщение
	sendto(sockFD, ACK, strlen(ACK), 0, clientAddr, clientAddrSize);

	//цикл чтения присылаемых нам сегментов и записи их в накопительный буфер
	for(i = 0; i < segNum; i++) {
		//читаем очередной сегмент
		int result = recvfrom(sockFD, tempBuffer, sizeof(tempBuffer), 0, clientAddr, &clientAddrSize);
		if(result > 0) {
			//записываем его в накопительный буфер
			strncat(accumBuffer, tempBuffer, strlen(tempBuffer));
			memset(&tempBuffer, 0, sizeof(tempBuffer));
			//отправляем клиенту подтверждение, запускающее пересылку следующего сегмента
			sendto(sockFD, ACK, strlen(ACK), 0, clientAddr, clientAddrSize);
		}
	}

	//возвращаем сокет в неблокирующий режим
	fdSetBlocking(sockFD, 0);

	//записываем в целевой буфер содержимое накопительного буфера
	snprintf(buffer, strlen(accumBuffer) + 1, "%s", accumBuffer);
}

void isMessageEntire(connection *connListItem, char *buffer) { //fix reading of the protocol signatures
	char tempString[10];
	memset(&tempString, 0, sizeof(tempString));

	int i, j;
	for(i = 0; i < 8; i++)
		tempString[i] = buffer[i];
	if(strncmp(tempString, PROTO_NAME, strlen(PROTO_NAME)) == 0) {
		memset(&tempString, 0, sizeof(tempString));
		for(i = 9, j = 0; i < 12; i++, j++)
			tempString[j] = buffer[i];
		if(strncmp(tempString, PROTO_VER, strlen(PROTO_VER)) == 0) {
			memset(&tempString, 0, sizeof(tempString));
			i++;
			j = 0;
			while(buffer[i] != '|') { //set field divider as the constant
				connListItem->length[j] = buffer[i];
				i++;
				j++;
			}
			if(strlen(buffer) < (size_t)atoi(connListItem->length)) //error output for atoi()
				connListItem->segmentationFlag = 1;
		}
	}
}

void Accumulator(connection *connListItem, char *buffer) {
	size_t currentLen = strlen(connListItem->storageBuffer) + strlen(buffer);
	size_t fullLen = (size_t)atoi(connListItem->length);
	if(currentLen == fullLen) {
		strcat(connListItem->storageBuffer, buffer);
		sprintf(buffer, "%s", connListItem->storageBuffer);
		memset(&connListItem->storageBuffer, 0, sizeof(connListItem->storageBuffer));
		connListItem->timeout = time(NULL);
		connListItem->segmentationFlag = 0;
	}
	else {
		strcat(connListItem->storageBuffer, buffer);
		connListItem->timeout = time(NULL);
	}
}
