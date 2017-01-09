#include <iostream>
#include <chrono>
#include <thread> 
#include <vector>
#include <mutex>
#include <kodocpp/kodocpp.hpp>
#include "PacketReciever.h"
#include "PacketSender.h"

#define FLOW1_SOURCE "8000"
#define FLOW1_DEST "7000"
#define FLOW2_SOURCE "9000"
#define FLOW2_DEST "6000"

class RecieverManager{
private: 
	PacketReciever reciever;
	PacketSender sender;
	std::vector<int> packetsRecievedPerBlock;
	typedef struct recvBlockInfo{
		uint32_t offset;			
		kodocpp::decoder decoder;
		uint32_t DOF;
	}recvBlockInfo;

	std::vector<recvBlockInfo> recievedBlocks;
public:

	RecieverManager(const char sourcePort[], const char destinationPort[]):
		reciever(atoi(sourcePort)),
		sender("127.0.0.1", destinationPort){
	}

	void recievePackets(){
		// Handles the recieving and acknowledgement of packsts on this flow
		// ACK must include
		// (1) seqNumber of the packet it is acknowleding
		// (2) first un-decoded block number
		// (3) the number of degrees of freedom in the first block number
		while (true){
			Packet p = reciever.listenOnce();
			std::cout << "Recieved packet SeqNum = " << p.seqNum << std::endl;

			Packet* ack = new Packet();

			ack->seqNum = p.seqNum;

			// TODO set DOF and current block
			
			sender.sendPacket(ack);
			std::cout << " ... Ack sent " << std::endl << std::endl;
			delete ack;
		}
	}
};

int main(){
	RecieverManager flow1(FLOW1_SOURCE, FLOW1_DEST);
	RecieverManager flow2(FLOW2_SOURCE, FLOW2_DEST);
	flow1.recievePackets();
}
