#include <iostream>
#include <chrono>
#include <thread> 

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
	int nextBlockToDecode = 0;

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
			std::cout << "Recieved packet SeqNum = " << p.seqNum;
			if(decodeBlock(&p) == 0){
				if((int)p.blockNo == nextBlockToDecode){
					nextBlockToDecode++;
				}
			}

			// TODO processPacket
			
			Packet* ack = new Packet();

			ack->seqNum = p.seqNum;
			ack->blockNo = nextBlockToDecode;

			// TODO set DOF and current block
			
			sender.sendPacket(ack);
			std::cout << " ... Ack sent ";
			std::cout << "blockNo = " << ack->blockNo << std::endl;
			delete ack;
		}
	}
	int decodeBlock(Packet *p){
		// Attempt to decode the block to which packet p belongs. return the 
		// degrees of freedom (0 if block decoded) 
		return (nextBlockToDecode*1000 -p->seqNum);
	}
};

int main(){
	RecieverManager flow1 = RecieverManager(FLOW1_SOURCE, FLOW1_DEST);
	RecieverManager flow2 = RecieverManager(FLOW2_SOURCE, FLOW2_DEST);
	flow1.recievePackets();
}
