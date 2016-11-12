#ifndef PACKETSENDER_H
#define PACKETSENDER_H
 
#include <sys/socket.h>


class PacketSender{

private:
	sockaddr_storage addrDest = {};
	int sock;	
	const char* destinationIp; 	
	const char* destinationPort;

public:
	PacketSender(const char* destIp, const char* destPort);
	~Pack	g++ -o sender main.o PacketSender.o
	g++ -o reciver PacketReciever.oetSender();
	int resolvehelper(const char* hostname, int family, const char* service, sockaddr_storage* pAddr);
	
	int prepareSocket();
	int sendPacket(const char* msg);
	int sendPacket(const char* msg, int length);
};

#endif
