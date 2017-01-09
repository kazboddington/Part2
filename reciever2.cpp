#include <iostream>
#include <chrono>
#include <thread> 
#include <vector>
#include <mutex>
#include <kodocpp/kodocpp.hpp>
#include <map>
#include <algorithm>

#include "PacketReciever.h"
#include "PacketSender.h"

#define FLOW1_SOURCE "8000"
#define FLOW1_DEST "7000"
#define FLOW2_SOURCE "9000"
#define FLOW2_DEST "6000"
#define PACKET_SIZE 1024

void printData(uint8_t data[], int length){
	std::cout << "Data looks like: ";
	for(int i = 0; i < length; i++)
		std::cout << std::hex << (int)data[i];
	std::cout << std::dec << std::endl;
}

class RecieverManager{
private: 
	PacketReciever reciever;
	PacketSender sender;
	std::vector<int> packetsRecievedPerBlock;
	typedef struct recvBlockInfo{
		uint32_t offset;			
		kodocpp::decoder decoder;
		std::vector<uint8_t> data_out;
		uint32_t DOF;
	}recvBlockInfo;

	std::map<uint32_t, recvBlockInfo*> recievedBlocksByOffset;

public:

	RecieverManager(const char sourcePort[], const char destinationPort[]):
		reciever(atoi(sourcePort)),
		sender("127.0.0.1", destinationPort){
	}


	recvBlockInfo* createInfoBlock(uint32_t blockSize, uint32_t offset){

		kodocpp::decoder_factory decoder_factory(
			kodocpp::codec::full_vector,
			kodocpp::field::binary8,
			blockSize,
			PACKET_SIZE);
		
		recvBlockInfo* blockInfo = new recvBlockInfo{
				offset,
				decoder_factory.build(),
				std::vector<uint8_t>(),
				blockSize};
		
		blockInfo->data_out.resize(blockInfo->decoder.block_size());


		blockInfo->decoder.set_mutable_symbols(
			blockInfo->data_out.data(), blockInfo->decoder.block_size());


		return blockInfo;
	}

	

	void recievePackets(){
		// Handles the recieving and acknowledgement of packsts on this flow
		// ACK must include
		// (1) seqNumber of the packet it is acknowleding
		// (2) first un-decoded block number
		// (3) the number of degrees of freedom in the first block number
		
		uint32_t blockSize = 250;

		while (true){
			Packet p = reciever.listenOnce();
			std::cout << "Recieved packet SeqNum = " << p.seqNum
				<< " Data size = " << p.dataSize << std::endl;

			// Block does not yet exist		
			if(recievedBlocksByOffset.find(p.offsetInFile) 
					== recievedBlocksByOffset.end()){
				// Block does not yet exist so create new block
				std::cout << "Making new block" << std::endl;

				recvBlockInfo* newBlock
				   	= createInfoBlock(blockSize, p.offsetInFile);
				
				std::cout << "Adding block to map" << std::endl;
				recievedBlocksByOffset.insert({p.offsetInFile, newBlock});
			}
		

			recvBlockInfo* theBlock	= recievedBlocksByOffset[p.offsetInFile]; 

			// Hand over data for the decoder
			std::vector<uint8_t> wrappedData(p.data, p.data + p.dataSize);
			theBlock->decoder.read_payload(wrappedData.data());

			// Decrement DOF for this block
			--(theBlock->DOF);
			theBlock->DOF = std::max((int)theBlock->DOF, 0);
			std::cout << "DOF = " << theBlock->DOF << std::endl;
	
			
			// create new packet and set fields 
			Packet* ack = new Packet();

			ack->currentBlockDOF = theBlock->DOF;
			ack->seqNum = p.seqNum;

			// TODO set DOF and current block
				
			sender.sendPacket(ack);

			if(theBlock->decoder.is_complete()){
				std::cout << "DECODED ALL DATA" << std::endl;
				printData(theBlock->data_out.data(), 50);
			}

			delete ack;
		}
	}
};


int main(){
	RecieverManager flow1(FLOW1_SOURCE, FLOW1_DEST);
	RecieverManager flow2(FLOW2_SOURCE, FLOW2_DEST);
	flow1.recievePackets();
}
