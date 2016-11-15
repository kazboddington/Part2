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

class TimerManager
{	
	typedef struct deltaElement
	{
		std::chrono::duration<double> timeRemaining;
		std::function<void()> callback;
	}deltaElement;
 	 
	std::list<deltaElement> deltaList;
	Clock clk;
public:
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
			std::cout << "time passed: " << timePassed.count() << "seconds" << std::endl;
			if(deltaList.size() > 0) {
				if(deltaList.front().timeRemaining < timePassed){
					deltaList.front().callback();
					std::chrono::duration<double> timeRemainingOnFirstItem
						= deltaList.front().timeRemaining;
					deltaList.pop_front();
					deltaList.front().timeRemaining -= timeRemainingOnFirstItem;
					
				}else{
					deltaList.front().timeRemaining -= timePassed;	
				}
			}
			t1 = t2;
		}
	}

	void addTask(std::chrono::duration<double> timeRemaining, std::function<void()> callback)
	{
		deltaList.push_front(deltaElement{timeRemaining, callback});		
	}

	
};

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
				std::cout << "Acknowledge recieved. Ack Num: " << p.ackNum;
				std::cout << "\tWindow Size: " << remainingWindow << std::endl;
				remainingWindow += 2000; 
				windowCV.notify_all();		
		}
	}
}
int main()
{
	//std::thread t1(sender);
	//recieveAcksAndAdjustWindow();
	//t1.join();
	TimerManager timerManager;	
	timerManager.manageTasks();
	return 0;
}

