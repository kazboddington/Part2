#include "PacketSender.h"
#include "PacketReciever.h"
#include <iostream>
#include <thread>
#include <string>	
#include <string.h>

void sender(){
	Packet p;
	unsigned char myChar = 50;
	p.type = myChar;
	PacketSender s = PacketSender("127.0.0.1","9000");

	s.sendPacket(&p);
}

int main()
{	
	
	std::thread t1(sender);

	
	PacketReciever reciever(9000);
	reciever.startListening();
	
	t1.join();

	return 0;
}

