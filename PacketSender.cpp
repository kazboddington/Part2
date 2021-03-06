#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <memory.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <errno.h>
#include <stdlib.h>
#include <iostream>
#include <chrono>

#include "PacketSender.h"

PacketSender::PacketSender(
	const char* destIp,
	const char* destPort): destinationIp(destIp), destinationPort(destPort)
{
	prepareSocket();	
}

PacketSender::~PacketSender(){
	close(sock);
}

int PacketSender::resolvehelper(
	const char* hostname, 
	int family,
	const char* service,
	sockaddr_storage* pAddr)
{
	int result;
	addrinfo* result_list = NULL;
	addrinfo hints = {};
	hints.ai_family = family;
	hints.ai_socktype = SOCK_DGRAM; // without this flag, getaddrinfo will return 3x the number of addresses (one for each socket type).
	result = getaddrinfo(hostname, service, &hints, &result_list);
	if (result == 0)
	{
		if((result_list->ai_addrlen <= sizeof(sockaddr_in)) == false) std::cout << "something wrong witht the address" << std::endl;
		memcpy(pAddr, result_list->ai_addr, result_list->ai_addrlen);
		freeaddrinfo(result_list);
	}

	return result;
}

void PacketSender::prepareSocket()
{
	// Set up socket and bind it to a  high random sending port
	int result = 0;
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	
	sockaddr_in addrListen = {}; // zero-int, sin_port is 0, which picks a random port for bind.
	addrListen.sin_family = AF_INET;
	addrListen.sin_port = 0; // htons(9999); Change this to send from a specific por  t

	result = bind(sock, (sockaddr*)&addrListen, sizeof(addrListen));
	if (result < 0)
	{
		int lasterror = errno;
		std::cout << "Error while binding: " << lasterror;
		perror("Error: ");
		exit(1);
	}

	// Set up destination addres
	result = resolvehelper(destinationIp, AF_INET, destinationPort,  &addrDest);
	if (result != 0)
	{
		int lasterror = errno;
		perror("Error: ");
		std::cout << "Error with result: result = " << result << " LastError: " << lasterror;		
		exit(1);
	}
}

void PacketSender::sendPacket(const char* msg)
{
	size_t msg_length = strlen(msg);
	sendPacket(msg, msg_length);
}

void PacketSender::sendPacket(const char* msg, int length)
{
	int result = sendto(sock, msg, length, 0, (sockaddr*)&addrDest, sizeof(addrDest));
	//std::cout << result << " bytes sent" << std::endl;
	if (result < 0){
		std::cout << "Error occurred: " << errno << std::endl;
		perror("Error: ");
	}		
}

void PacketSender::sendPacket(Packet *p)
{
	// Set the timePoint inside the packer to now
	size_t msg_length = sizeof(Packet);
	sendPacket(reinterpret_cast<const char*>(p), msg_length);
}
