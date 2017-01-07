#define DESTINATION_PORT "9000"
#define SOURCE_PORT "10000"
#define LOSS_PROBABILITY 0.01
#define DELAY 100 /* NOTE defined in ms */

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

int main(){
	PacketReciever reciever(atoi(SOURCE_PORT));
	PacketSender sender("127.0.0.1",DESTINATION_PORT);
	
	TimerManager timer;
	std::thread timerThread(&TimerManager::manageTasks, std::ref(timer));
	std::chrono::duration<double, std::milli> delay(DELAY);	
	std::srand(std::time(NULL));
	while (true){
		Packet p = reciever.listenOnce();
		std::cout << "Recieved Packet!" << std::endl;
		if(LOSS_PROBABILITY != 0){
			int rand = std::rand() % 100;
			std::cout << rand << std::endl;
			if (rand != 1){
				std::function<void()> resendPacket = std::bind(sendPacket, p, std::ref(sender));
				timer.addTask(delay, resendPacket);
				std::cout << "Packet sent afer delay" << std::endl;
			}else{
				std::cout << "Packet Lost due to random loss" << std::endl;
			}
		}else{
			std::function<void()> resendPacket = std::bind(sendPacket, p, std::ref(sender));
			timer.addTask(delay, resendPacket);
			std::cout << "Packet sent afer delay" << std::endl;
		}
	}
	
		
	timerThread.join();
	return 0;
}
