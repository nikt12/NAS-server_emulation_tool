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

error errTable[20];

void handleErr(short errCode) {
	int i, n;
	for(i = 0; i < sizeof(errTable)/sizeof(errTable[0]); i++)
		if(errTable[i].errCode == errCode) {
			n = i;
			break;
		}
	fprintf(stderr, "%s", errTable[n].errDesc);
	if((abs(errCode) <= 7))
		exit(EXIT_FAILURE);
}

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

void strToLower(char *str) {
	int i;
	for(i = 0; str[i]; i++){
	  str[i] = tolower(str[i]);
	}
}

int checkArgs(char *port, char *transport) {
	strToLower(transport);
	if((atoi(port) < 1024) || (atoi(port) > 65535) || ((strcmp(transport, "udp") != 0) && (strcmp(transport, "tcp") != 0))) {
		return -1;
	}
	return 0;
}
