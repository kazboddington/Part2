#ifndef PACKETRECIEVER_H
#define PACKETRECIEVER_H

#include <netinet/in.h>
#include "packetStruct.h"

class PacketReciever{

private:
	struct sockaddr_in myaddr;
	struct sockaddr_in remoteaddr;
	int recvlen;
	unsigned char buf[2000];
	socklen_t addrlen = sizeof(remoteaddr);
	int sockfd;	
	int port;

public:
	PacketReciever(int p);
	void startListening();
	Packet listenOnce();
};

#endif
