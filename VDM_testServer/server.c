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
#include <signal.h>
#include <syslog.h>
#include <libconfig.h>
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
	struct sigaction sa;
	sigset_t newset;

	sigemptyset(&newset);
	//sigaddset(&newset, SIGHUP);
	sigprocmask(SIG_BLOCK, &newset, 0);
	sa.sa_handler = sig_handler;
	sigaction(SIGINT, &sa, 0);
	sigaction(SIGHUP, &sa, 0);

	do {
		endEventLoop = 0;
		int listeningSocket, result, i;
		connection connList[NUM_OF_CONNECTIONS];
		config_t cfg;
		config_setting_t *setting;
		int port;
		const char *transport;
		int qlen;

		memset(&connList, 0, sizeof(connList));

		errTableInit();

		config_init(&cfg);

		if (!config_read_file(&cfg, "user_config.cfg")) {
			fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg), config_error_line(&cfg), config_error_text(&cfg));
			openlog("TestSignals", LOG_PID | LOG_CONS, LOG_DAEMON);
				syslog(LOG_INFO, "Reading conf is crashed.");
				closelog();
			config_destroy(&cfg);
			return (EXIT_FAILURE);
		}

		 /*Поиск строчки port */
		if (config_lookup_int(&cfg, "application.connectSocket.port", &port))
			printf("Store port: %d\n", port);
		else
			fprintf(stderr, "No 'port' setting in configuration file.\n");

		/* Поиск строчки transport */
		if (config_lookup_string(&cfg, "application.connectSocket.transport", &transport))
			printf("Store transport: %s\n", transport);
		else
			fprintf(stderr, "No 'transport' setting in configuration file.\n");

		 /*Поиск строчки qlen */
		if (config_lookup_int(&cfg, "application.connectSocket.qlen", &qlen))
			printf("Store qlen: %d\n", qlen);
		else
			fprintf(stderr, "No 'qlen' setting in configuration file.\n");

		listeningSocket = createServerSocket(port, transport, qlen);
		if(listeningSocket < 0)
			handleErr(listeningSocket);
		if(strcmp(transport, "tcp") == 0) {
			printf("Waiting for connections... Listening on port %d with queue length %d...\n", port, qlen);
			eventLoopTCP(connList, listeningSocket);
		}
		else {
			printf("Using UDP protocol. Waiting for connections on port %d with queue length %d...\n", port, qlen);
			eventLoopUDP(connList, listeningSocket);
		}

		for(i = 0; i < NUM_OF_CONNECTIONS; i++)
			if(connList[i].clientNickName[0] != '\0') {
				result = write(connList[i].clientSockFD, srvIsOffline, strlen(srvIsOffline));
				if(result < 0)
					return result;
				close(connList[i].clientSockFD);
			}
		close(listeningSocket);
	}
	while(!endMainLoop);
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

	while(!endEventLoop) {
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

	while(!endEventLoop) {
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

void checkIpStack(config_t cfg, char *checkName, char *checkAddr)
{
	config_setting_t *ipsets;
	ipsets = config_lookup(&cfg, "services.ipsets"); //read ipsets

	if(ipsets != NULL)
	{
		int count = config_setting_length(ipsets); //get number for ipsets
		int i, n;

		for (i = 0; i < count; ++i) //go over ipsets
		{
			config_setting_t *ipset = config_setting_get_elem(ipsets, i); //take ipset #i

			char name;
			config_setting_t *ipaddresses;
			char addr;

			if(!(config_setting_lookup_string(ipset, "name", &name)) // check if can read name
				&& (ipaddresses = config_lookup(ipset, "addresses"))) // check if can read addresses
			{
				if(checkName == name) // check if name of service is matching
				{
					printf("Name of service is matching".);
					int count_n = config_setting_length(ipaddresses); // get number of addresses

					for (n = 0; n < count_n;++ n) // go-over-addresses
					{
						addr = config_setting_get_string_elem(ipaddresses, n); // get address #n
						if (checkAddr == addr) // check if address is matching with given
						{
							printf("Address is matching.");
						}
					}
				}
				continue; // next ipset
			} // end of ipset checker
		} // end of go-over-sets loop
	} // end of ipsets checker
} // end of void
