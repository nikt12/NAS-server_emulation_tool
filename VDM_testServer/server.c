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

#define EPOLL_QUEUE_LEN 100		//длина очереди событий epoll
#define MAX_EPOLL_EVENTS 100	//максимальное число событий epoll за одну итерацию
#define EPOLL_RUN_TIMEOUT 5		//таймаут ожидания событий (-1 соответствует ожиданию до первого наступившего события)

void eventLoopTCP(connection *connList, int listeningSocket);

void eventLoopUDP(connection *connList, int serverSock);

int main(int argc, char *argv[]) {
	int listeningSocket;										//дескриптор сокета сервера
	connection connList[NUM_OF_CONNECTIONS];			//массив структур с данными о клиентах и соединений с ними

	//инициализируем используемые строки и структуры нулями
	memset(&connList, 0, sizeof(connList));

	//проверяем, получено ли необходимое количество аргументов
	if (argc == 4) {
		int result = argCheck(argv[1], argv[2]);
		if(result != 0)
			perror("wrong args");
		//создаем сокет сервера и выполняем его привязку с переводом в режим прослушивания
		listeningSocket = createServerSocket(argv[1], argv[2], argv[3]);
		//проверяем значение дескриптора сокета
		if (listeningSocket < 0)
			return -1;
		else {
			if(strcmp(argv[2], "tcp") == 0)
				eventLoopTCP(connList, listeningSocket);
			else
				eventLoopUDP(connList, listeningSocket);
		}
	}
	else
		//если введено неверное количество аргументов, выводим правильный формат запуска программы
		printf("Usage: %s port transport query_length\n", argv[0]);
	return 0;
}

void eventLoopTCP(connection *connList, int listeningSocket) {
	int i;
	printf("Сервер вошел в режим прослушивания сокета. Ожидание подключений...\n");
	int epollFD = epoll_create(EPOLL_QUEUE_LEN);			//создаем epoll-инстанс
	struct epoll_event event;								//создаем структуру событий epoll
	struct epoll_event evList[MAX_EPOLL_EVENTS];		//создаем массив структур событий epoll
	event.events = EPOLLIN;								//указываем маску интересующих нас событий
	event.data.fd = listeningSocket;							//указываем файловый дескриптор для мониторинга событий
	epoll_ctl(epollFD, EPOLL_CTL_ADD, listeningSocket, &event);	//начинаем мониторинг с заданными параметрами

	//цикл мониторинга событий epoll
	while (1) {
		//ожидаем событий, в nfds будет помещено число готовых файловых дескрипторов
		int readyFDs = epoll_wait(epollFD, evList, MAX_EPOLL_EVENTS, EPOLL_RUN_TIMEOUT);

		timeoutCheck(connList, evList);

		//цикл проверки готовых файловых дескрипторов
		for(i = 0; i < readyFDs; i++) // add EPOLLHUP, EPOLLERR
			//проверяем, на каком дескрипторе произошло событие
			if (evList[i].data.fd == listeningSocket)
				acceptNewConnection(listeningSocket, connList, epollFD, &event);
			else
				dataExchangeTCP(connList, &evList[i]);
	}
}

void eventLoopUDP(connection *connList, int serverSock) {
	int i;
	struct epoll_event event;								//создаем структуру событий epoll
	struct epoll_event evList[MAX_EPOLL_EVENTS];		//создаем массив структур событий epoll

	printf("Работа про протоколу UDP. Ожидание подключений...\n");
	int epfd = epoll_create(EPOLL_QUEUE_LEN);			//создаем epoll-инстанс
	event.events = EPOLLIN;								//указываем маску интересующих нас событий
	event.data.fd = serverSock;							//указываем файловый дескриптор для мониторинга событий
	epoll_ctl(epfd, EPOLL_CTL_ADD, serverSock, &event);	//начинаем мониторинг с заданными параметрами

	while(1) {
		//ожидаем событий, в nfds будет помещено число готовых файловых дескрипторов
		int readyFDs = epoll_wait(epfd, evList, MAX_EPOLL_EVENTS, EPOLL_RUN_TIMEOUT);

		for(i = 0; i < readyFDs; i++)
			if(evList[i].events & EPOLLIN)
				dataExchangeUDP(serverSock, connList, &evList[i]);
	}
}
