#ifndef PACKETSENDER_H
#define PACKETSENDER_H
 
#include <sys/socket.h>


typedef struct Packets{
	unsigned char type;
	unsigned char dataSize;
	unsigned int seqNum;
	unsigned int ackNum;
	unsigned short windowNum;
	unsigned char data[1024];	
}Packet;

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
	
	int prepareSocket();
	int sendPacket(const char* msg);
	int sendPacket(const char* msg, int length);
	int sendPacket(Packet *p);
};

#endif
