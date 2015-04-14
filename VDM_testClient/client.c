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
#include "clientFunctions.h"

void exchangeLoopTCP(connection *conn, int sockFD);

void exchangeLoopUDP(connection *conn, int sockFD, struct sockaddr_in *serverAddr, socklen_t serverAddrSize);

int main(int argc, char *argv[]) {
	int sockFD, portNo;
	connection conn;
	struct sockaddr_in serverAddr;
	socklen_t serverAddrSize;

    //char buffer1[BUFFERSIZE];

	//инициализируем используемые строки и структуры нулями
	memset(&conn, 0, sizeof(conn));
	memset(&serverAddr, 0, sizeof(serverAddr));
	//memset(&buffer1, 0, sizeof(buffer1));

	strcpy(conn.protoName, PROTO_NAME);
	strcpy(conn.protoVersion, PROTO_VER);

    //проверяем, получено ли необходимое количество аргументов
    if (argc == 4) {
    	if(argCheck(argv[2], argv[3]) != 0)
    		perror("wrong args");
        //вызываем функцию создания сокета и подключения к хосту
    	sockFD = createClientSocket(argv[1], argv[2], argv[3]);

    	if(strcmp(argv[3], "udp") == 0)
        	printf("Client was started. Using UDP protocol!\n\n");

    	getClientInfo(&conn);

        if(strcmp(argv[3], "tcp") == 0) {
        	exchangeLoopTCP(&conn, sockFD);
        }
        else {
            portNo = atoi(argv[2]);
            serverAddr.sin_port = htons(portNo);
        	serverAddr.sin_family = AF_INET;
        	inet_pton(AF_INET, argv[1], &serverAddr.sin_addr);
        	serverAddrSize = sizeof(serverAddr);
        	exchangeLoopUDP(&conn, sockFD, &serverAddr, serverAddrSize);
        }

        // закрываем файловый дескриптор сокета
    	close(sockFD);
    }
    else
    	//если введено неверное количество аргументов, выводим правильный формат запуска программы
    	printf("Usage: %s address port transport\n", argv[0]);

    return 0;
}

void exchangeLoopTCP(connection *conn, int sockFD) {
    while(1) {
    	if(getMessageText(conn) == -1)
    		break;

    	sendMessageToServerTCP(sockFD, conn);

		/*strncpy(buffer1, buffer, 20);
		n = write(sockFD, buffer1, strlen(buffer1));
		memset(&buffer1, 0, sizeof(buffer1));
		int i, j;
		for(i = 20, j = 0; i < 30; i++, j++)
			buffer1[j]=buffer[i];
		sleep(5);
		n = write(sockFD, buffer1, strlen(buffer1));
		memset(&buffer1, 0, sizeof(buffer1));
		for(i = 30, j = 0; i < strlen(buffer); i++, j++)
			buffer1[j]=buffer[i];
		sleep(5);
		n = write(sockFD, buffer1, strlen(buffer1));
		memset(&buffer1, 0, sizeof(buffer1));*/

		//к этому моменту на стороне сервера наш сокет уже сделан неблокирующим, и, чтобы дождаться ответа
		//от сервера, нужно вернуть его в блокирующий режим
		fdSetBlocking(sockFD, 1);

		if(recvMessageFromServerTCP(sockFD, conn) == -2)
			break;

		//возвращаем сокет в блокирующий режим для корректной работы сервера
		fdSetBlocking(sockFD, 0);
	}
}

void exchangeLoopUDP(connection *conn, int sockFD, struct sockaddr_in *serverAddr, socklen_t serverAddrSize) {
    while(1) {
    	if(getMessageText(conn) == -1)
    		break;

		sendMessageToServerUDP(sockFD, conn, serverAddr, serverAddrSize);

		//к этому моменту на стороне сервера наш сокет уже сделан неблокирующим, и, чтобы дождаться ответа
		//от сервера, нужно вернуть его в блокирующий режим
		fdSetBlocking(sockFD, 1);

		if(recvMessageFromServerUDP(sockFD, conn, serverAddr, serverAddrSize) == -2)
			break;

		//возвращаем сокет в блокирующий режим для корректной работы сервера
		//fd_set_blocking(sockFD, 0);
    }
}
