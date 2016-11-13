#include "PacketSender.h"
#include "PacketReciever.h"
#include <iostream>
#include <thread>
#include <string>	
#include <string.h>

#define SOURCE_PORT "10000"	
#define DESTINATION_PORT "9000"

/* ENDPOINT 2 - RECIEVING PACKETS FROM ENDPOINT 1 */

void sender(){
	Packet p;
	PacketSender s = PacketSender("127.0.0.1", DESTINATION_PORT);
	int x = 0;
	for(x; x < 1000; ++x){
		std::cout << "Sending Packet: "<< x << std::endl;
		p.seqNum = x;
		s.sendPacket(&p);
	}
}

int main()
{	
	// Prepare ACK to send in response
	Packet p;
	p.type = ACK;
	p.windowSize += 2000;
	
	PacketSender s = PacketSender("127.0.0.1", DESTINATION_PORT);

	PacketReciever reciever(atoi(SOURCE_PORT));
	while(true){
		Packet packet = reciever.listenOnce();
		p.ackNum = packet.seqNum;
		s.sendPacket(&p);
		std::cout << "Sending acknowledgent...\n seqNum = " << packet.seqNum << std::endl;
	}
	
	return 0;
}

