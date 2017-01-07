#ifndef PACKETSENDER_H
#define PACKETSENDER_H
 
#include <sys/socket.h>
#include "packetStruct.h"

class PacketSender{

private:
	sockaddr_storage addrDest = {};
	int sock;	
	const char* destinationIp; 	
	const char* destinationPort;

public:

	PacketSender(const char* destIp, const char* destPort);
	~PacketSender();
	int resolvehelper(const char* hostname, int family, const char* service, sockaddr_storage* pAddr);
	
	void prepareSocket();
	void sendPacket(const char* msg);
	void sendPacket(const char* msg, int length);
	void sendPacket(Packet *p);
};

#endif
