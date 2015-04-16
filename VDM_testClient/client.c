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
#include "commonFunctions.h"

int exchangeLoopTCP(connection *conn, int sockFD);

int exchangeLoopUDP(connection *conn, int sockFD, struct sockaddr_in *serverAddr, socklen_t serverAddrSize);

int main(int argc, char *argv[]) {
	int sockFD, portNo, result;
	connection conn;
	struct sockaddr_in serverAddr;
	socklen_t serverAddrSize;

	//инициализируем используемые строки и структуры нулями
	memset(&conn, 0, sizeof(conn));
	memset(&serverAddr, 0, sizeof(serverAddr));

	strcpy(conn.protoName, PROTO_NAME);
	strcpy(conn.protoVersion, PROTO_VER);

	errTableInit();

    //проверяем, получено ли необходимое количество аргументов
    if (argc == 4) {
    	result = checkArgs(argv[2], argv[3]);
		if(result < 0)
			handleErr(result);
        //вызываем функцию создания сокета и подключения к хосту
    	sockFD = createClientSocket(argv[1], argv[2], argv[3]);
    	if(sockFD < 0)
    		handleErr(sockFD);

    	getClientInfo(&conn);

        if(strcmp(argv[3], "tcp") == 0) {
        	while(1) {
        		result = exchangeLoopTCP(&conn, sockFD);
        		if(result < 0)
        			handleErr(result);
        	}
        }
        else {
            portNo = atoi(argv[2]);
            serverAddr.sin_port = htons(portNo);
        	serverAddr.sin_family = AF_INET;
        	inet_pton(AF_INET, argv[1], &serverAddr.sin_addr);
        	serverAddrSize = sizeof(serverAddr);
        	while(1) {
        		result = exchangeLoopUDP(&conn, sockFD, &serverAddr, serverAddrSize);
        		if(result < 0)
        			handleErr(result);
        	}
        }
    	close(sockFD);
    }
    else
    	printf("Usage: %s address port transport\n", argv[0]);

    return 0;
}

int exchangeLoopTCP(connection *conn, int sockFD) {
	int result;

	result = getMessageText(conn);
	if(result < 0)
		return result;

	result = sendMessageToServerTCP(sockFD, conn);
	if(result < 0)
		return result;

	fdSetBlocking(sockFD, 1);
	if(sockFD < 0)
		return -5;

	result = recvMessageFromServerTCP(sockFD, conn);
	if(result < 0)
		return result;

	fdSetBlocking(sockFD, 0);
	if(sockFD < 0)
		return -5;

	return 0;
}

int exchangeLoopUDP(connection *conn, int sockFD, struct sockaddr_in *serverAddr, socklen_t serverAddrSize) {
	int result;

	result = getMessageText(conn);
	if(result < 0)
		return result;

	result = sendMessageToServerUDP(sockFD, conn, serverAddr, serverAddrSize);
	if(result < 0)
		return result;

	fdSetBlocking(sockFD, 1);
	if(sockFD < 0)
		return -5;

	result = recvMessageFromServerUDP(sockFD, conn, serverAddr, serverAddrSize);
	if(result < 0)
		return result;

	//fd_set_blocking(sockFD, 0);

	return 0;
}
