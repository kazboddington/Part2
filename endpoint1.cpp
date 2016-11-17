#include "PacketSender.h"
#include "PacketReciever.h"
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

#define SOURCE_PORT "9000"
#define DESTINATION_PORT "10000"

/* ENDPOINT 1 - SEDNING PACKETS TO ENDPOINT */ 

typedef std::chrono::high_resolution_clock Clock;

typedef struct SenderWindow{
	int startByte;						/* The beginning byte of the window */
	int windowSize;						/* The size of the window */
	int sentTo;							/* The NEXT byte to send (i.e. starts at byte 0) */
	std::mutex windowMutex;				/* Mutex to protect window's access */
	std::condition_variable windowCV;   /* Condition Variable to allow for waiting on window's changes */
} SenderWindow;

SenderWindow window;

class TimerManager
{	
	typedef struct deltaElement
	{
		std::chrono::duration<double> timeRemaining;
		std::function<void()> callback;
	}deltaElement;
 	std::mutex deltaListMutex;
	std::list<deltaElement> deltaList;
	Clock clk;
public:

	/* Managaing tasks involves continuously sleep and move through the deltaList */
	/* We call the callback when a deltaList item expires        */
	void manageTasks()
	{
		Clock::time_point t1 = Clock::now();
		while(true){
			const struct timespec waitime = {0, 1000000};
			struct timespec remaingTime;
			nanosleep(&waitime, &remaingTime);			
			Clock::time_point t2 = Clock::now();	
			std::chrono::duration<double> timePassed = 
				std::chrono::duration_cast<std::chrono::duration<double>>(t2-t1);
			//std::cout << "time passed: " << timePassed.count() << "seconds" << std::endl;
			deltaListMutex.lock();
			while(deltaList.size() > 0 && timePassed > std::chrono::duration<double>::zero()){
				deltaList.front().timeRemaining -= timePassed;	

				if(deltaList.front().timeRemaining < std::chrono::duration<double>::zero()){
					deltaList.front().callback();
					timePassed = deltaList.front().timeRemaining;
					if (timePassed.count() < 0) timePassed = -timePassed;
					deltaList.pop_front();
				}else{
					break;
				}	
			}
			deltaListMutex.unlock();
			t1 = t2;
		}
	}

	void addTask(std::chrono::duration<double> delay, std::function<void()> callback)
	{	
		std::lock_guard<std::mutex> deltaListLockGuard(deltaListMutex);	
		std::cout << "adding task. deltaList Length:" << deltaList.size()<< std::endl;
		if(deltaList.size() == 0){
			deltaElement newElement{delay, callback};
			deltaList.push_front(newElement);
			std::cout << "Packet added to empyy deltaList. Calling in " << delay.count() << " seconds" << std::endl;
			return;
		}
		for(std::list<deltaElement>::iterator it = deltaList.begin(); it != deltaList.end(); ++it){
			delay -= it->timeRemaining;	
			if (delay < std::chrono::duration<double>::zero()){
				// Got to the place in the list that the element needs to go.
				delay += it->timeRemaining;
				it->timeRemaining -= delay;
				deltaElement newElement{delay, callback};
				std::cout << "inserting element" << std::endl;
				deltaList.insert(it, newElement); 
				std::cout << "Adding new task. Will be called in " << delay.count() << " seconds." << std::endl;
				return;
			}
		}
		deltaList.push_back(deltaElement{delay, callback});	
	
		
	}
	void removeTask(){
		//TODO			
	}
};


class SenderThread{
	/* This thread is used manage the output buffer and send packets safely */

	std::list<Packet> outputBuffer;
	std::mutex bufferMutex;
	int bufferCounter = 0;
	std::condition_variable bufferCV;	
	PacketSender packetSender;
public:
	SenderThread(PacketSender &s): packetSender(s){}

	
	std::thread spawn(){
		return std::thread([=]{ mainLoop(); });
	}				
	
	void mainLoop(){
	/* Loop through buffer constantly, sending buffered packets */	
		std::unique_lock<std::mutex> lk(bufferMutex);	
		while(true){
			bufferCV.wait(lk, [this]{return bufferCounter > 0;});
			Packet nextToSend = outputBuffer.back();
			outputBuffer.pop_back();
			bufferCounter--;
			bufferCV.notify_all();
			packetSender.sendPacket(&nextToSend);
			std::cout << "Packet Sent. seqNum = "<< nextToSend.seqNum << std::endl;
		}
	}

	void sendPacket(Packet p){
	/* This should add the packet to the output buffer, ready to be sent when it can. */
		{
			std::lock_guard<std::mutex> lock(bufferMutex);
			outputBuffer.push_front(p);
			bufferCounter++;
			std::cout << "Packet added to output Queue. seqNum = "<< p.seqNum << std::endl;
		}
		bufferCV.notify_all();
	}
};

/* The shared sender for this endpoint */


PacketSender packetSender = PacketSender("127.0.0.1",DESTINATION_PORT);
 

void printSomething(){
	std::cout << "WORKED" << std::endl;
}

/* Adjust the window, resend packet that has been lost */
void noAckRecievedCallback(int sequenceNumber){
	Packet p;
	p.seqNum = sequenceNumber;
	packetSender.sendPacket(&p);
	std::cout << "No Ack recieved - resending packet " << sequenceNumber << std::endl;
}

void sendBufferWindowed(std::vector<char> &dataToSend, SenderThread &senderObject){
	
	std::cout << "sending windowed data. DataSize = " << dataToSend.size()<< std::endl;
	window.windowCV.notify_all();
	std::unique_lock<std::mutex> lock(window.windowMutex);
	bool allDataSent = false;
	while(!allDataSent){
		window.windowCV.wait(lock, []{return window.sentTo < window.windowSize;});	
		while(window.sentTo < window.windowSize){
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
				std::min(window.sentTo - window.startByte + window.windowSize,  1024),
				(int)dataToSend.size());
			p.seqNum = window.sentTo;

			int endByte = window.sentTo + p.dataSize;
			std::copy(dataToSend.begin() + window.sentTo, dataToSend.end() + endByte, p.data);
		
			//adjust sentTo pointer since we've sent a packet
			window.sentTo += p.dataSize;	

			//send the packet
			senderObject.sendPacket(p);
			std::cout << "Sent packet: dataSize = " << p.dataSize << " seqNum = " << p.seqNum << std::endl;
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
	window.windowSize = 2000;
	window.startByte = 0;
	window.sentTo = 0;
	
	// Make packet sender to consume output packets and spawn it
	PacketSender packetSender = PacketSender("127.0.0.1",DESTINATION_PORT);
	SenderThread senderThreadObj(packetSender);
	std::thread	senderThread = senderThreadObj.spawn();

	// Queue callback on the timer manager
	TimerManager timerManager;	
	int i = 0;
	std::function<void()> manageFunction = std::bind(&TimerManager::manageTasks, &timerManager);
	std::thread timerThread(manageFunction);
	std::vector<char> dataToSend = readFileIntoBuffer("./fileToSend.txt");		
	sendBufferWindowed(dataToSend, senderThreadObj);
//	for(i; i<22; ++i){
//		std::chrono::duration<double> timeForTask(i);
//		Packet p;
//		p.seqNum = i*10;
//		p.dataSize = 10;
//		for(int j = 0; j < 1024; ++j) p.data[j] = 'H';
//		//create calback on timer. The callback queue a packet to be sent by the sender thread	
//		std::function<void(Packet)> sendBlankPacket1 = 
//			(std::bind(&SenderThread::sendPacket, &senderThreadObj, std::placeholders::_1));
//		std::function<void()> sendBlankPacket2 = (std::bind(sendBlankPacket1, p));
//		timerManager.addTask(timeForTask, sendBlankPacket2);
//	}
	
	timerThread.join();	
	senderThread.join();
	return 0;
}
