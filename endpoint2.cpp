#include "PacketSender.h"
#include "PacketReciever.h"
#include <iostream>
#include <thread>
#include <string>	
#include <string.h>
#include <mutex>
#include <vector>

#define SOURCE_PORT "10000"	
#define DESTINATION_PORT "9000"

/* ENDPOINT 2 - RECIEVING PACKETS FROM ENDPOINT 1 */

typedef struct RecieverWindow{
	unsigned int recievedTo;			/* The start byte of the window, as seen by the reciever */
	unsigned int windowSize;			/* The current size of the window, in bytes */
	std::mutex windowMutex; /* A mutex to protect the window object*/
} RecieverWindow;

std::vector<unsigned char> recievedData;
std::vector<bool> bytesRecieved;

RecieverWindow window;

unsigned int calculteRecievedTo(){ //note assumes already have safe access to window data
	int counter = window.recievedTo;
	for(counter;  counter < bytesRecieved.size(); ++counter){
		if (bytesRecieved[counter] == false) break;
	}
	return counter;
}

int main()
{	
	// Prepare ACK to send in response
	Packet p;
	p.type = ACK;
	
	PacketSender s = PacketSender("127.0.0.1", DESTINATION_PORT);

	PacketReciever reciever(atoi(SOURCE_PORT));
	while(true){
		Packet packet = reciever.listenOnce();
		
		// Introduce artificial delay before acknowledging (not realistic currently)	
		const struct timespec waitime = {0, 100000000};
		struct timespec remaingTime;
		//nanosleep(&waitime, &remaingTime);			
		
		std::cout << "Packet Recieved. seqNum = " << packet.seqNum;
		std::cout << " dataSize = " << packet.dataSize;
		std::cout << " RecievedTo = " << window.recievedTo << std::endl;
		window.windowMutex.lock();
		if(window.recievedTo <= (packet.seqNum + packet.dataSize)){ // Check it's not already been recieved	
			if(recievedData.size() < packet.seqNum + packet.dataSize){
				std::cout << "Resizing to " << packet.seqNum + packet.dataSize << std::endl;
				recievedData.resize(packet.seqNum + packet.dataSize);
				bytesRecieved.resize(packet.seqNum + packet.dataSize);
			}
			// The packet recieved is not before the window
			for(int j = 0; j < packet.dataSize; ++j){
				recievedData[j + packet.seqNum] = packet.data[j];
				bytesRecieved[j + packet.seqNum] = true;
			}
		}
		
		window.recievedTo = calculteRecievedTo(); 
		std::cout << "Recalculated where we're recieved to as: " << window.recievedTo << std::endl;
		p.ackNum = window.recievedTo + 1; //Request NEXT byte
		p.seqNum = packet.seqNum; // Tells sender which packet this was in response to, helping calculate RTT		
		window.windowMutex.unlock();	
		s.sendPacket(&p);
		std::cout << "Sending acknowledgent... ackNum = " << p.ackNum << std::endl;
		std::cout << "The data looks like: ";
		for (unsigned char c : recievedData){
			std::cout << c;
		}
		std::cout << std::endl;	
	}
	
	return 0;
}

