#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <syslog.h>
#include <libconfig.h>
#include "crc.h"
#include "protocol.h"
#include "servFunctions.h"

int errno;

error errTable[20];

int endEventLoop = 0, endMainLoop = 0;

void errTableInit() {
	errTable[0].errCode = -1;
	errTable[0].errDesc = "Incorrect program arguments!\n";
	errTable[1].errCode = -2;
	errTable[1].errDesc = "Error creating the socket!\n";
	errTable[2].errCode = -3;
	errTable[2].errDesc = "Error switching socket FD to blocking mode!\n";
	errTable[3].errCode = -4;
	errTable[3].errDesc = "Error setting option to socket FD!\n";
	errTable[4].errCode = -5;
	errTable[4].errDesc = "Can't bind!\n";
	errTable[5].errCode = -6;
	errTable[5].errDesc = "Error switching socket to the listening state!\n";
	errTable[6].errCode = -7;
	errTable[6].errDesc = "Can't accept the connection with client!\n";
	errTable[7].errCode = -8;
	errTable[7].errDesc = "Can't accept the connection with client: no more place!\n";
	errTable[8].errCode = -9;
	errTable[8].errDesc = "Can't identify the client!\n";
	errTable[9].errCode = -10;
	errTable[9].errDesc = "Error reading new message from client!\n";
	errTable[10].errCode = -11;
	errTable[10].errDesc = "Checksum missmatch!\n";
	errTable[11].errCode = -12;
	errTable[11].errDesc = "Non-existing service was requested by the client!\n";
	errTable[12].errCode = -13;
	errTable[12].errDesc = "Error sending message!\n";
}

void timeoutCheck(connection *connList, struct epoll_event *evList) {
	time_t timeout;
	int j = 0;

	for(j = 0; j < NUM_OF_CONNECTIONS; j++) {
		timeout = time(NULL) - connList[j].timeout;
		if((timeout > TIMEOUT) && (connList[j].clientHostName[0] != '\0')) {
			openlog("NAS-server-emulator", LOG_PID | LOG_CONS, LOG_DAEMON);
			syslog(LOG_INFO, "Timeout period for \"%s\" has experied.\n", connList[j].clientHostName);
			closelog();
			close(evList[j].data.fd);
			memset(&connList[j], 0, sizeof(connList[j]));
			break;
		}
	}
}

//реализация функции создания и связывания сокета
//аргументы:
//port - порт, с которым связывается сервер
//transport - протокол, по которому будет работать сервер
//qlen - длина очереди на подключение к сокету
int createServerSocket(int port, const char *transport, int qlen) {
	struct sockaddr_in sin;			//структура IP-адреса
	int s, result, type, proto, q_len, optval = 1;				//дескриптор и тип сокета

	//q_len = atoi(qlen);

	memset(&sin, 0, sizeof(sin));

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons((unsigned short)port);

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
		return -2;

	fdSetBlocking(s, 0);
	if (s < 0)
		return -3;

	result = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	if(result < 0)
		return -4;

	result = bind(s, (struct sockaddr *)&sin, sizeof(sin));
	if(result < 0)
		return -5;

	if(type == SOCK_STREAM) {
		result = listen(s, qlen);
		if(result < 0)
			return -6;
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
			return -7;
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
				openlog("NAS-server-emulator", LOG_PID | LOG_CONS, LOG_DAEMON);
				syslog(LOG_INFO, "Accepted connection on descriptor %d (hostname: %s)!\n", clientSocket, clientName);
				closelog();
				break;
			}
			else
				if(j == NUM_OF_CONNECTIONS-1) {
					write(clientSocket, connStructOverflowNotification, strlen(connStructOverflowNotification));
					return -8;
				}
		}
		return clientSocket;
	}
}

int identifySenderTCP(connection *connList, struct epoll_event *evListItem) {
	int i, n = -9;
	for (i = 0; i < NUM_OF_CONNECTIONS; i++)
		if (evListItem->data.fd == connList[i].clientSockFD) {
			n = i;
			break;
		}
	return n;
}

int identifySenderUDP(connection *connList, char *buffer) {
	int i, k, n = -9;
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
		return -11;
}

int firstServiceTCP(connection *connListItem, struct epoll_event *evListItem, char *buffer) {
	int result;
	printf("%s: %s\n", connListItem->clientNickName, connListItem->messageText);
	strcat(connListItem->messageText, firstSrvResponse);
	Serializer(connListItem, buffer);
	result = write(evListItem->data.fd, buffer, strlen(buffer));
	if(result < 0)
		return -13;
	return result;
}

int firstServiceUDP(int serverSock, connection *connListItem, struct sockaddr *clientAddr, socklen_t clientAddrSize, char *buffer) {
	int result;
	printf("%s: %s\n", connListItem->clientNickName, connListItem->messageText);
	strcat(connListItem->messageText, firstSrvResponse);
	checkIpStack(clientAddr->sa_data, connListItem->serviceName);
	Serializer(connListItem, buffer);
	result = sendto(serverSock, buffer, strlen(buffer), 0, clientAddr, clientAddrSize);
	if(result < 0)
		return -13;
	return result;
}

int secondServiceTCP(connection *connListItem, struct epoll_event *evListItem, char *buffer) {
	int result;
	printf("%s: %s\n", connListItem->clientNickName, connListItem->messageText);
	strcat(connListItem->messageText, secondSrvResponse);
	Serializer(connListItem, buffer);
	result = write(evListItem->data.fd, buffer, strlen(buffer));
	if(result < 0)
		return -13;
	return result;
}

int secondServiceUDP(int serverSock, connection *connListItem, struct sockaddr *clientAddr, socklen_t clientAddrSize, char *buffer) {
	int result;
	printf("%s: %s\n", connListItem->clientNickName, connListItem->messageText);
	strcat(connListItem->messageText, secondSrvResponse);
	checkIpStack(clientAddr->sa_data, connListItem->serviceName);
	Serializer(connListItem, buffer);
	result = sendto(serverSock, buffer, strlen(buffer), 0, clientAddr, clientAddrSize);
	if(result < 0)
		return -13;
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
			return -10;
		return 0;
	}
	if (result == 0) {
		openlog("NAS-server-emulator", LOG_PID | LOG_CONS, LOG_DAEMON);
		syslog(LOG_INFO, "Client \"%s\" (at %s) has closed the connection.\n", connList[n].clientNickName, connList[n].clientHostName);
		closelog();
		close(evListItem->data.fd);
		memset(&connList[n], 0, sizeof(connList[n]));
		return 0;
	}

	result = readingInParts(&connList[n], buffer);		//fix reading in parts
	if(result == 0)
		return 0; //some err code should be here

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
			return -12;
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
			return -10;
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
			return -12;
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

void sig_handler(int signum) {
	switch (signum) {
	case SIGINT:
		//printf("\nServer is closing...\n");
		openlog("NAS-server_emulator", LOG_PID | LOG_CONS, LOG_DAEMON);
		syslog(LOG_INFO, "Daemon is off.");
		closelog();
		endEventLoop = 1;
		endMainLoop = 1;
		return;
	case SIGHUP:
		openlog("NAS-server-emulator", LOG_PID | LOG_CONS, LOG_DAEMON);
		syslog(LOG_INFO, "Restarting daemon. Reloading config...");
		closelog();
		//printf("\nRestarting program. Reloading config...\n", signum);
		endEventLoop = 1;
		return;
	}
}

/* imagine that we have cfg, and client asks for service through  interface */
int checkIpStack(const char *serverInterface, const char *serviceName) // check if service is allowed through given interface
{
	config_setting_t *ipset;
	char defaultServucesPath[25] = "application.services."; //constant for services in cfg,(why we need const char *???)
	//char *ipset_path = (char *) malloc(100); // make address for desired service, (we really don't need this)
	char ipsetPath[100];
	strcpy(ipset_path, DEFAULT_SERVICES_PATH); // add DEFAULT_SERVICES_PATH
	strcat(ipset_path, serverInterface); // add name of interface
	strcat(ipset_path, ".serviceNames"); //read service, if exists

	if(ipset != NULL) // if ipset isn't empty
	{
		int count = config_setting_length(ipset); //get number of services
		int i;
		for (i = 0; i < count; i++) //go over services on interface(i use damn i++)
		{
			const char *service= config_setting_get_string_elem(ipset, i); //take service #i from cfg and compare with given
			if (!strcmp(service,serviceName)) //check if asked and taken services match
			{
				return 1; // service is found
			}
			continue; // go to next service
		} // end of go-over-services loop
	}
	else
	{
		return 0; // if ip set is NULL
	}
	return 2; // service is not found
} // end of function
