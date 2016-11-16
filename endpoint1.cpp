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

#define SOURCE_PORT "9000"
#define DESTINATION_PORT "10000"

/* ENDPOINT 1 - SEDNING PACKETS TO ENDPOINT */ 

typedef std::chrono::high_resolution_clock Clock;
std::mutex windowMutex;
std::condition_variable windowCV;
short remainingWindow = 2120; // Initalised to be this value - would usually be from 3 way handshake 

typedef struct Window{
	int startByte;
	int windowSize;
	int section;
	std::mutex windowMutex;
} window;


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
 
//void sender(){
//	Packet p;
//	int x = 0;
//
//	std::unique_lock<std::mutex> windowLock(windowMutex);
//	for(x; x < 10; ++x){
//		/* Window lock is for the remaining window.                                        */
//		/* This thread waits on the window lock to be above 0 if enough data has been sent */
//		std::cout << "Window Size: " << remainingWindow << std::endl; 
//		if (remainingWindow < 0){
//			std::cout << "Sent all I can at the moment, waiting for ACK..." << std::endl;
//			while(remainingWindow < 0) windowCV.wait(windowLock);
//		}
//		std::cout << "Sending Packet: " << x << std::endl;
//		p.seqNum = x;
//		packetSender.sendPacket(&p);
//		remainingWindow -= sizeof(Packet);
//	}
//}
//


void recieveAcksAndAdjustWindow()
{
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
				std::cout << "Acknowledge recieved. AckNum: " << p.ackNum;
				std::cout << "\tWindow Size: " << remainingWindow << std::endl;
				remainingWindow += 2000; 
				windowCV.notify_all();		
		}
	}
}

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

int main()
{
	//std::thread t1(sender);
	//recieveAcksAndAdjustWindow();
	//t1.join();
	

	// Make packet sender to consume output packets and spawn it
	PacketSender packetSender = PacketSender("127.0.0.1",DESTINATION_PORT);
	SenderThread senderThreadObj(packetSender);
	std::thread	senderThread = senderThreadObj.spawn();


	//TODO move the timer to a separate thread

	// Queue callback on the timer manager
	TimerManager timerManager;	
	int i = 0;
	std::function<void()> manageFunction = std::bind(&TimerManager::manageTasks, &timerManager);
	std::thread timerThread(manageFunction);
	for(i; i<10000; ++i){
		std::chrono::duration<double, std::nano> timeForTask(i);
		Packet p;
		p.seqNum = i;
		//create calback on timer. The callback queue a packet to be sent by the sender thread	
		std::function<void(Packet)> sendBlankPacket1 = 
			(std::bind(&SenderThread::sendPacket, &senderThreadObj, std::placeholders::_1));
		std::function<void()> sendBlankPacket2 = (std::bind(sendBlankPacket1, p));
		std::cout << "calling time: " << i << std::endl;
		timerManager.addTask(timeForTask, sendBlankPacket2);
	}
	
	senderThread.join();
	return 0;
}
