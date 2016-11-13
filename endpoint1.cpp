#include "PacketSender.h"
#include "PacketReciever.h"

#define SOURCE_PORT 9000
#define DESTINATION_PORT 10000

void sender(){
	Packet p;
	PacketSender s = PacketSender("127.0.0.1","SOURCE_PORT");
	int x = 0;
	for(x; x < 1000; ++x){
		p.seqNum = x;
		s.sendPacket(&p);
	}
	
}

int main()
{	
	std::thread t1(sender);

	PacketReciever reciever(DESTINATION_PORT);
	
	int x = 0;
	for(x; x<1000; ++x){
		Packet firstPacket = reciever.listenOnce();
		std::cout << firstPacket.seqNum << std::endl;
	}
	
	t1.join();

	return 0;
}


