#include <iostream>
#include <thread>
#include <string>	
#include <string.h>
#include <mutex>
#include <condition_variable>
#include <list>
#include <ctime>
#include <chrono>	
#include <vector>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <iostream>
#include <unordered_map>

#include "PacketSender.h"
#include "PacketReciever.h"
#include "SenderThread.h"
#include "TimerManager.h"

#define SOURCE_PORT "9000"
#define DESTINATION_PORT "10000"

/* ENDPOINT 1 - SEDNING PACKETS TO ENDPOINT */ 


/* Contains the timestamps that each packet was sent at	*/
std::unordered_map<unsigned int, std::chrono::high_resolution_clock::time_point> packetSentTimes;	


typedef struct SenderWindow{
	unsigned int startByte;						/* The beginning byte of the window */
	unsigned int windowSize;						/* The size of the window */
	unsigned int sentTo;							/* The NEXT byte to send (i.e. starts at byte 0) */
	std::mutex windowMutex;				/* Mutex to protect window's access */
	std::condition_variable windowCV;   /* Condition Variable to allow for waiting on window's changes */
} SenderWindow;
SenderWindow window; // This is the window shared window obect


/* The shared sender for this endpoint */

void resendPacket(Packet p, SenderThread &senderObject, TimerManager &timerManager){
	// NOTE currently don't take any lock for the window 
	// since we only read it - may need rectifying in future
	std::cout << "Checking to see if packet needs resending" << std::endl;
	std::cout << "\t seqNum = " << p.seqNum << " Window starts at byte " << window.startByte << std::endl;
	if(p.seqNum >= window.startByte){
		senderObject.sendPacket(p);
		
		// Change packet sent time since we're retransmitting the packet	
		auto now = std::chrono::high_resolution_clock::now();
		packetSentTimes[p.seqNum] = now;

		// Need to schedule for packet to be resent again	
		std::chrono::duration<double> timeForTask(3);
		std::function<void()> timeoutRetransmit(std::bind(resendPacket, p, 
					std::ref(senderObject), std::ref(timerManager)));
		timerManager.addTask(timeForTask, timeoutRetransmit);
	}
}

void sendBufferWindowed(std::vector<char> &dataToSend, SenderThread &senderObject, TimerManager &timerManager){

	std::cout << "sending windowed data. DataSize = " << dataToSend.size()<< std::endl;
	//window.windowCV.notify_all();
	//std::unique_lock<std::mutex> lock(window.windowMutex);
	bool allDataSent = false;
	while(!allDataSent){
		//TODO sort out locking on the window
		//window.windowCV.wait(lock, []{return window.sentTo < window.windowSize;});	
		while(window.sentTo < (window.windowSize + window.startByte)){
			std::cout << "Window: sendTo = " << window.sentTo <<	" windowSize = ";
			std::cout << window.windowSize << " startByte = " << window.startByte << std::endl;
			//Check if we've sent all the data
			if(window.sentTo >= dataToSend.size()){ 
				std::cout << "Sent all data!" << std::endl;
				allDataSent = true;	
				break; 
			}
			// Set up packet to send
			Packet p;
			p.type = DATA;
			p.dataSize = std::min(
					std::min(window.startByte + window.windowSize - window.sentTo, (unsigned int)10),
					(unsigned int)dataToSend.size());
			p.seqNum = window.sentTo;

			int endByte = window.sentTo + p.dataSize;
			std::copy(dataToSend.begin() + window.sentTo, dataToSend.end() + endByte, p.data);

			//adjust sentTo pointer since we've sent a packet
			window.sentTo += p.dataSize;	


			//send the packet
			senderObject.sendPacket(p);
				
			// Add timestamp to packetSentTimes
			auto now = std::chrono::high_resolution_clock::now();
			packetSentTimes[p.seqNum] = now;
			
			std::cout << "Sent packet: dataSize = " << p.dataSize << " seqNum = " << p.seqNum << std::endl;

			// Schedule task of resending packet (will not occur if ACK recieved)	
			std::chrono::duration<double> timeForTask(3);
			std::function<void()> timeoutRetransmit(std::bind(resendPacket, p, 
				std::ref(senderObject), std::ref(timerManager)));
			timerManager.addTask(timeForTask, timeoutRetransmit);
		}	
	}
}


std::vector<char> readFileIntoBuffer(std::string filename){
	std::ifstream input(filename, std::ios::binary);
	return std::vector<char>((std::istreambuf_iterator<char>(input)),
		(std::istreambuf_iterator<char>()));
}

int main()
{
	// initialise window values	
	window.windowSize = 30;
	window.startByte = 0;
	window.sentTo = 0;
	
	// Make packet sender to consume output packets and spawn it
	PacketSender packetSender = PacketSender("127.0.0.1",DESTINATION_PORT);
	SenderThread senderThreadObj(packetSender);
	std::thread	senderThread = senderThreadObj.spawn();

	// Queue callback on the timer manager
	TimerManager timerManager;	
	std::function<void()> manageFunction = std::bind(&TimerManager::manageTasks, &timerManager);
	std::thread timerThread(manageFunction);
	std::vector<char> dataToSend = readFileIntoBuffer("./fileToSend.txt");		
	std::thread windowManagerThread(sendBufferWindowed, std::ref(dataToSend),
		std::ref(senderThreadObj), std::ref(timerManager));
	
	// Listen for acknowledgements and update the window	
	PacketReciever reciever(atoi(SOURCE_PORT));
	while(true){
		//TODO something needs to be done to lock the window
		
		Packet p = reciever.listenOnce();
		std::cout << "Recieved ACK, ackNum = " << p.ackNum << std::endl;

		// Calculate the RTT and update the accumulated one
		auto got = packetSentTimes.find(p.seqNum); 
		if(got == packetSentTimes.end()){
			std::cout << "Packet not found in packetSentTimes" << std::endl;
		}else{
			auto rtt = std::chrono::high_resolution_clock::now() - got->second;
			std::cout << "Packet with seqNum " << got->first << " and RTT " ;
		   	std::cout << std::chrono::duration_cast<std::chrono::microseconds>(rtt).count();
		   	std::cout << " microseconds"	<<  std::endl;

		}
		
		window.startByte = std::max(window.startByte, p.ackNum);	
		window.windowCV.notify_all();
		if(window.startByte >= dataToSend.size()){
			std::cout << "ALL DATA HAS BEEN ACKNOWLEDGED!!! FINISHED!!!" << std::endl;
			break;
		}
	}
	
	windowManagerThread.join();
	timerThread.join();	
	senderThread.join();
	return 0;
}
