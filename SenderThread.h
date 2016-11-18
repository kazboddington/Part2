#ifndef SENDER_THREAD
#define SENDER_THREAD

#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <iostream>

#include "packetStruct.h"
#include "PacketSender.h"

class SenderThread{
	/* This thread is used manage the output buffer and send packets safely */

	std::list<Packet> outputBuffer;
	std::mutex bufferMutex;
	int bufferCounter = 0;
	std::condition_variable bufferCV;	
	PacketSender packetSender;
public:
	SenderThread(PacketSender &s);
	std::thread spawn();	
	void mainLoop();
	void sendPacket(Packet p);
};
#endif 
