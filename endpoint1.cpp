#include "PacketSender.h"
#include "PacketReciever.h"
#include <iostream>
#include <thread>
#include <string>	
#include <string.h>
#include <mutex>
#include <condition_variable>

#define SOURCE_PORT "9000"
#define DESTINATION_PORT "10000"
/* ENDPOINT 1 - SEDNING PACKETS TO ENDPOINT */ 
std::mutex windowMutex;
std::condition_variable windowCV;
int remainingWindow = 2120;

void sender(){
	Packet p;
	PacketSender s = PacketSender("127.0.0.1",DESTINATION_PORT);
	int x = 0;
	for(x; x < 10; ++x){
		std::unique_lock<std::mutex> windowLock(windowMutex);			
		std::cout << "Window Size: " << remainingWindow << std::endl; 
		if (remainingWindow < 0){
			std::cout << "Sent all I can at the moment, waiting for ACK..." << std::endl;
			while(remainingWindow < 0) windowCV.wait(windowLock);
		}
		std::cout << "Sending Packet: " << x << std::endl;
		p.seqNum = x;
		s.sendPacket(&p);
		remainingWindow -= sizeof(Packet);
	}
	
}

int main()
{	
	std::thread t1(sender);

	PacketReciever reciever(atoi(SOURCE_PORT));
	int x = 0;
	for(;;){
		Packet p = reciever.listenOnce();
		switch(p.type)
		{
			case(DATA):
				std::cout << p.dataSize << " data bytes recieved: " << p.data << std::endl;
				break;
			case(ACK):
				std::cout << "Acknowledge recieved. Ack Num: " << p.ackNum;
				std::cout << "\tWindow Size: " << remainingWindow << std::endl;
				remainingWindow += 2000; 
				windowCV.notify_all();		
		}
	}

	
	t1.join();

	return 0;
}

