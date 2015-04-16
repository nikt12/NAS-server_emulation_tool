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
#include "crc.h"
#include "protocol.h"
#include "servFunctions.h"
#include "commonFunctions.h"

#define EPOLL_QUEUE_LEN 100
#define MAX_EPOLL_EVENTS 100
#define EPOLL_RUN_TIMEOUT 5

void eventLoopTCP(connection *connList, int listeningSocket);

void eventLoopUDP(connection *connList, int serverSock);

int main(int argc, char *argv[]) {
	int listeningSocket, result;
	connection connList[NUM_OF_CONNECTIONS];

	memset(&connList, 0, sizeof(connList));

	errTableInit();

	if (argc == 4) {
		result = checkArgs(argv[1], argv[2]);
		if(result != 0)
			handleErr(result);
		listeningSocket = createServerSocket(argv[1], argv[2], argv[3]);
		if(listeningSocket < 0)
			handleErr(listeningSocket);
		if(strcmp(argv[2], "tcp") == 0) {
			printf("Waiting for connections... Listening on port %s with queue length %s...\n", argv[1], argv[3]);
			eventLoopTCP(connList, listeningSocket);
		}
		else {
			printf("Using UDP protocol. Waiting for connections on port %s with queue length %s...\n", argv[1], argv[2]);
			eventLoopUDP(connList, listeningSocket);
		}
	}
	else
		printf("Usage: %s port transport query_length\n", argv[0]);
	return 0;
}

void eventLoopTCP(connection *connList, int listeningSocket) {
	int i, epollFD, readyFDs, result;
	struct epoll_event event;
	struct epoll_event evList[MAX_EPOLL_EVENTS];

	event.events = EPOLLIN;
	event.data.fd = listeningSocket;

	epollFD = epoll_create(EPOLL_QUEUE_LEN);
	epoll_ctl(epollFD, EPOLL_CTL_ADD, listeningSocket, &event);

	while (1) {
		readyFDs = epoll_wait(epollFD, evList, MAX_EPOLL_EVENTS, EPOLL_RUN_TIMEOUT);

		timeoutCheck(connList, evList);

		for(i = 0; i < readyFDs; i++) { // add EPOLLHUP, EPOLLERR
			if (evList[i].data.fd == listeningSocket) {
				while(1) {
					result = acceptNewConnection(listeningSocket, connList, epollFD, &event);
					if(result < 0)
						handleErr(result);
					break;
				}
			}
			else {
				while(1) {
					result = dataExchangeTCP(connList, &evList[i]);
					if(result < 0)
						handleErr(result);
					break;
				}
			}
		}
	}
}

void eventLoopUDP(connection *connList, int serverSock) {
	int i, epollFD, readyFDs, result;
	struct epoll_event event;
	struct epoll_event evList[MAX_EPOLL_EVENTS];

	epollFD = epoll_create(EPOLL_QUEUE_LEN);

	event.events = EPOLLIN;
	event.data.fd = serverSock;

	epoll_ctl(epollFD, EPOLL_CTL_ADD, serverSock, &event);

	while(1) {
		readyFDs = epoll_wait(epollFD, evList, MAX_EPOLL_EVENTS, EPOLL_RUN_TIMEOUT);

		for(i = 0; i < readyFDs; i++)
			if(evList[i].events & EPOLLIN) {	//add EPOLLERR...
				while(1) {
					result = dataExchangeUDP(serverSock, connList, &evList[i]);
					if(result < 0)
						handleErr(result);
					break;
				}
			}
	}
}
