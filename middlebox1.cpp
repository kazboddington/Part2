#define DELAY 1000 /* NOTE defined in ms */

#include <thread>
#include <chrono>

#include "PacketReciever.h"
#include "PacketSender.h"
#include "TimerManager.h"
#include "packetStruct.h"

/* This thread simply forwads pakcets from the given port to the given destination address. */
/* It adds delay and loss rate based upon the defined parameters at the top. 				*/

void sendPacket(Packet p, PacketSender &packetSender){
	packetSender.sendPacket(&p);
}

int main(int argc, char *argv[]){
	if (argc != 5){
		std::cout << "Usage middlebox [IN_PORT] [OUT_PORT] "
			<< "[LOSS_PROB] [DELAY(ms)]" << std::endl;	
		return 0;
	}
		
	char* sourcePort = argv[1];
	char* destinationPort = argv[2];
	char* lossProbability = argv[3];
	char* userDelay = argv[4];

	PacketReciever reciever(atoi(sourcePort));
	std::cout << "Listening on port " << sourcePort << std::endl; 
	PacketSender sender("127.0.0.1",destinationPort);
	std::cout << "Sending on port " << destinationPort << std::endl; 

	TimerManager timer;
	std::thread timerThread(&TimerManager::manageTasks, std::ref(timer));
	std::chrono::duration<double, std::milli> delay(atoi(userDelay));	
	std::srand(std::time(NULL));

	int totalPackets = 0;	
	int numberOfLosses = 0;

	while (true){
		Packet p = reciever.listenOnce();
		totalPackets++;

		auto packetDelay = delay*0.95 + 
			std::chrono::duration<double, std::milli>(
					std::rand()%((int)(atoi(userDelay)*0.1)));
		std::cout << "delay added" << packetDelay.count() << std::endl;

		if(lossProbability != 0){
			int rand = std::rand() % (int)(1/atof(lossProbability));
			
			if (rand != 0){
				std::function<void()> resendPacket 
					= std::bind(sendPacket, p, std::ref(sender));
				timer.addTask(
						packetDelay,
					  	resendPacket);
			}else{
				numberOfLosses++;
			}

		}else{
			std::function<void()> resendPacket 
				= std::bind(sendPacket, p, std::ref(sender));
			timer.addTask(packetDelay, resendPacket);
		}

		std::cout << "totalPackets = " << totalPackets
			<< " losses = " << numberOfLosses << std::endl;
		
	}
	
		
	timerThread.join();
	return 0;
}
