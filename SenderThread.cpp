#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <iostream>

#include "packetStruct.h"
#include "PacketSender.h"
#include "SenderThread.h"

SenderThread::SenderThread(PacketSender &s): packetSender(s){}


std::thread SenderThread::spawn(){
	return std::thread([=]{ mainLoop(); });
}				

void SenderThread::mainLoop(){
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

void SenderThread::sendPacket(Packet p){
	/* This should add the packet to the output buffer, ready to be sent when it can. */
	{
		std::lock_guard<std::mutex> lock(bufferMutex);
		outputBuffer.push_front(p);
		bufferCounter++;
		std::cout << "Packet added to output Queue. seqNum = "<< p.seqNum << std::endl;
	}
	bufferCV.notify_all();
}

