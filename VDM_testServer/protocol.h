#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#define MTU 1500		//максимальный размер передаваемых данных
#define TIMEOUT 30		//тайм-аут в секундах

#define PROTO_NAME "NAS_SRV_EMULATOR"	//имя протокола
#define PROTO_VER "0.1"					//версия прокола

#define NUM_OF_CONNECTIONS 100	//число активных соединений
#define NUM_OF_SEGMENTS 100		//максимальное число сегментов

#define BUFFERSIZE 2048			//длина буфера
#define MSGSIZE 1536			//длина сообщения
#define CRC32SIZE 10			//длина контрольной суммы
#define NICK_SIZE 17			//длина никнейма
#define SERVICE_SIZE 10			//длина имени сервиса

extern const char segmentationWarning[];
extern const char ACK[];

extern const char firstServiceName[];
extern const char secondServiceName[];

extern const char firstSrvResponse[];
extern const char secondSrvResponse[];

extern const char wrongSrvNotification[];
extern const char connStructOverflowNotification[];
extern const char crcMissmatchNotification[];

//структура, описывающая соединение и его параметры
typedef struct {
	char protoName[10];					//имя протокола
	char protoVersion[5];				//версия протокола
	char length[5];						//длина сообщения
	int clientSockFD;					//файловый дескриптор клиентского сокета
	char clientHostName[17];			//хостнейм клиента
	char clientNickName[NICK_SIZE];		//ник пользователя
	char serviceName[SERVICE_SIZE];		//имя сервиса
	char messageText[MSGSIZE];			//текст сообщения
	char messageCRC32[CRC32SIZE];		//контрольная сумма
	int timeout;						//тайм-аут
	char storageBuffer[BUFFERSIZE];
	short segmentationFlag;
} connection;

void timeoutCheck(connection *connList, struct epoll_event *evList);

void Serializer(connection *connection, char *buffer);

void deSerializer(connection *connListItem, char *buffer);

int Divider(int sockFD, char *buffer, struct sockaddr_in *serverAddr, socklen_t serverAddrSize);

void Assembler(int sockFD, char *buffer, struct sockaddr *clientAddr, socklen_t clientAddrSize);

void isMessageEntire(connection *connListItem, char *buffer);

void Accumulator(connection *connListItem, char *buffer);

#endif /* PROTOCOL_H_ */
