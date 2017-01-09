#include <iostream>
#include <chrono>
#include <vector>
#include <thread> 
#include <unistd.h>
#include <algorithm>
#include <list>
#include <mutex>

#include <kodocpp/kodocpp.hpp>
#include "netcode/decoder.hh"
#include "PacketSender.h"
#include "PacketReciever.h"

#define FLOW1_DEST "4000"
#define FLOW2_DEST "5000"
#define FLOW1_SOURCE "2000"
#define FLOW2_SOURCE "3000"
#define PACKET_SIZE 1024

// Forward declarations
class SenderFlowManager;

// Declaration of senderManager
class SenderManager{
private:
	std::vector<SenderFlowManager*> subflows;
	int numberOfBlocks = 1000;
	std::vector<uint8_t>& dataToSend; 
	int sentUpTo = 0;
public:
	SenderManager(std::vector<uint8_t>& dataToSend);
	void addSubflow(SenderFlowManager *subflow);
	int getNumberOfBlocks();
	std::vector<SenderFlowManager *> &getSubflows();
	// Hands over a block of data for a flow to send to the reciever of the
	// asked for size. 
	std::vector<uint8_t> getDataToSend(int dataSize);
};

// Declaratin and implementatino of flowManager
class SenderFlowManager{
private:
	PacketSender sender;
	PacketReciever reciever;
	SenderManager &manager;

	int seqNumNext = 0; // An index of the next packet to be sent
	int lastSeqNumAckd = 0; // The last seqence number to be acknowledged
	int tokens = 5; // The number of packets availble to be sent
	int DOFCurrentBlock = 0; // Degrees of freedom of current block

  	const double alpha = 0.1; // Used for updating loss probability
	double lossProbability = 0;

	typedef struct blockInfo{
		std::mutex blockInfoMutex; // Must Lock this before acccessing info
		std::vector<uint8_t> data; // Data being sent, controlled by encoder
		uint32_t offsetInFile; // Where in the original file this data is from
		kodocpp::encoder encoder;
		uint32_t ackedDOF;

		// TODO not used yet, but will be used to replace current way of
		// calculating the number of packets in flight per block
		uint32_t numberOfPacketsinFlight; 
	}blockInfo;

	std::list<blockInfo*> currentBlocksBeingSent; 
	// Info for each block beingg sent by this flow
	// Note that we delete blocks from this once they're sent
	
	std::vector<std::chrono::high_resolution_clock::time_point> sentTimeStamps;
	std::vector<blockInfo*> mapSeqNumToBlockInfo;
	
public:
	// The four connections held by the sender module

	typedef struct RttInfo{
		std::chrono::duration<double> average;	
		/* Running average of rtt estimation */
		std::chrono::duration<double> deviataion;
		/* Deviation of rtt estiamation */
		std::chrono::duration<double> min;
		/* Smallest RTT seen so far */
	}RttInfo;
	RttInfo rttInfo = {
		std::chrono::duration<double>(1),
		std::chrono::duration<double>(0),
		std::chrono::duration<double>(1)};

	SenderFlowManager(
		const char sourcePort[],
		const char destinationPort[],
		SenderManager& theManager):
		sender(PacketSender("127.0.0.1", destinationPort)),		
		reciever(PacketReciever(atoi(sourcePort))),
		manager(theManager){
	
		std::cout << "Flow manager set up to listen on port "
			<< sourcePort << " and to send on port "
			<< destinationPort << std::endl;
	}

	void recieveAndProcessAcks(){
		// Do three things here 
		// (1) Update RTT
		// (2) Check for timeouts
		// (3) Update lastSeqNumAckd, blockNumber, current DOF.
		// (4) Increament tokens
		while(true){
			Packet p = reciever.listenOnce();
			std::cout << "Ack recieved, seqNum = " << p.seqNum << std::endl;	

			// Adjust RTT
			auto packetRtt = std::chrono::high_resolution_clock::now()
			   - sentTimeStamps[p.seqNum]; 
			adjustRtt(packetRtt);

			// Adjust loss probability
			lossProbability -= lossProbability*(1-alpha)*(1-alpha) + alpha;

			// TODO check for timeouts	
			
			// Adjust  lastSeqNumAcked, currentDOF
			lastSeqNumAckd = std::max(lastSeqNumAckd, (int)p.seqNum);

			tokens++;
		}
	}

	void sendLoop(){
		// Naiive implementation - poll tokens, and send packet if token free
		while(true){
			if(tokens != 0){ // Allowed to send

				// Create packet to send, setting fields etc.
				Packet *p = calculatePacketToSend();
				std::cout << "Packet calculated to send" << std::endl;

				// Save timestamp 
				sentTimeStamps.push_back( 
					std::chrono::high_resolution_clock::now());

				// Send packet
				sender.sendPacket(p);
				std::cout << "Packet sent, seqNum = " << p->seqNum;

				// Reduce number of tokens since a packet is sent 
				tokens--;

				// Clean up
				tokens--;
				delete p;
			}
		}
	}

	void checkForLosses(){
		// Loops, looking at the packets that are currently in flight,
		// checking to see if there are losses
		
		int seqNum = lastSeqNumAckd + 1;			
		while(true){
			usleep(1000); // loop every 1 millisecond 

			// No need to loop if we've already recieved the all acks 
			if(seqNum >= seqNumNext) continue;

			auto now = std::chrono::high_resolution_clock::now();

			// Loop thorough packets in flight, oldest to newest. Look for 
			// high-delay packets and count them as lost	
			// Note this doesn't use std deviation, which would be a better idea
			while(seqNum <= seqNumNext &&
				rttInfo.average*1.5 < (now-sentTimeStamps[seqNum])){
				
				std::cout << "Loss Detected... " << std::endl;

				adjustForLostPacket(mapSeqNumToBlockInfo[seqNum]);
				
				// Update the lastSeqNumAcked as if packet is acked
				lastSeqNumAckd++;

				seqNum++;
			}
		}
	}
 
	void adjustForLostPacket(blockInfo* blockLostFrom){
		// Adjust the loss probability and the RTT, as well as increment tokens
		blockLostFrom->blockInfoMutex.lock();
		blockLostFrom->numberOfPacketsinFlight -= 1;
		blockLostFrom->blockInfoMutex.unlock();
		lossProbability -= lossProbability*(1-alpha)*(1-alpha) + alpha;
		tokens++;
	}


	Packet* calculatePacketToSend(){
		// Creates packet on heap of the correct data to send 
		// TODO calculate which packet should be sent next and code it
		// NOTE, should set block number to correct value
			
		// NOTE: Assume that the encoder is set up correctly
		
		Packet *p = new Packet();
		p->seqNum = seqNumNext;
		seqNumNext++;

		// Iterate through block infos until we find a block in which not enough
		// packets have been sent for the reciever to decode 
			
		std::cout << "currentBlocksBeingSent length = "
			<< currentBlocksBeingSent.size() << std::endl;


		std::list<blockInfo*>::iterator it;
		for(it = currentBlocksBeingSent.begin(); 
			it != currentBlocksBeingSent.end(); 
			++it){
			
			std::cout << "Grabbing lock mutex... " << std::endl;
			(*it)->blockInfoMutex.lock();
			std::cout << "Mutex aquired" << std::endl;
			int expectedArrivals = (*it)->numberOfPacketsinFlight*lossProbability
				+ (*it)->ackedDOF;
			if(expectedArrivals < (int)(*it)->data.size()){
				(*it)->encoder.write_payload(p->data);
				p->offsetInFile = (*it)->offsetInFile;
				mapSeqNumToBlockInfo.push_back((*it));
				return p;
			}
		}

		// None of the blocks in the list need packets sending. Create a new 
		// block and generate a packet using it.
		currentBlocksBeingSent.push_back(calculateNewBlock());

		currentBlocksBeingSent.back()->blockInfoMutex.lock(); // Take lock
		
		// Generate packet from encoder
		currentBlocksBeingSent.back()->encoder.write_payload(p->data);
		
		// Update number of packets in flight since we're sendin a packet
		(currentBlocksBeingSent.back()->numberOfPacketsinFlight)++;

		// set the packt's offset field
		p->offsetInFile = currentBlocksBeingSent.back()->offsetInFile;

		// Add the packet's seqnece number to the map of seqence numbers
		mapSeqNumToBlockInfo.push_back(currentBlocksBeingSent.back());
		currentBlocksBeingSent.back()->blockInfoMutex.unlock(); // release lock

		return p;
	}

	void adjustRtt(std::chrono::duration<double, std::micro> measurement){
		std::chrono::duration<double> error = measurement - rttInfo.average;
		rttInfo.average = rttInfo.average + 0.125*error;
		if(error.count() < 0) error = -error;
		rttInfo.deviataion += 0.125*(error- rttInfo.deviataion);
		if(measurement < rttInfo.min) rttInfo.min = measurement;
		std::cout << "RTT update: Average "
			<< rttInfo.average.count() << " s";
		std::cout << " Deviation : " 
			<< rttInfo.deviataion.count() << " s";
		std::cout << " Min : " << rttInfo.min.count() 
			<< " s" << std::endl;
	}
	
	double getLossProbability(){
		return lossProbability;
	}
	
	blockInfo* calculateNewBlock(){
		blockInfo* theBlock = new blockInfo();

		// Temporart fixed length blocks of 1000 packets
		uint32_t numberofPacketsInBlock = 1000;

		// Grab data from manager
		theBlock->data = manager.getDataToSend(numberofPacketsInBlock);
		std::cout << "Got data to send" << std::endl;
		std::cout << " looks like: " << theBlock->data.data() << std::endl;
		// TODO set offset in blockInfo

		// Create and encoder and accociate it with the data
		kodocpp::encoder_factory encoder_factory(
			kodocpp::codec::full_vector,
			kodocpp::field::binary8,
			numberofPacketsInBlock,
			PACKET_SIZE);
		theBlock->encoder = encoder_factory.build();
		theBlock->encoder.set_const_symbols(
				theBlock->data.data(),
				theBlock->data.size());

		theBlock->ackedDOF = numberofPacketsInBlock;
		
		return theBlock;
	}
	
};

// Implementation of SenderManager

SenderManager::SenderManager(std::vector<uint8_t>& theData): 
	dataToSend(theData){
}

void SenderManager::addSubflow(SenderFlowManager *subflow){
	subflows.push_back(subflow);
	std::cout << "Added Flow to subflows, Size = "
		<< subflows.size() << std::endl;
}

int SenderManager::getNumberOfBlocks(){
	return numberOfBlocks;
}

std::vector<SenderFlowManager *> &SenderManager::getSubflows(){
	return subflows;
}

std::vector<uint8_t> SenderManager::getDataToSend(int blockSizeWanted){
	int amountToSend = std::min(
			(int)(dataToSend.size() - sentUpTo),
			(int)blockSizeWanted);
	std::cout << "Handing over " << amountToSend 
		<< " bytes to flow" << std::endl;
	return std::vector<uint8_t>(
			dataToSend.begin() + sentUpTo,
			dataToSend.begin() + sentUpTo + amountToSend);
}


int main(){
	std::vector<uint8_t> dataToSend(100000);
	std::generate(dataToSend.begin(), dataToSend.end(), rand);

	SenderManager manager(dataToSend);

	SenderFlowManager flow1 = 
		SenderFlowManager(FLOW1_SOURCE, FLOW1_DEST, manager);
	SenderFlowManager flow2 = 
		SenderFlowManager(FLOW2_SOURCE, FLOW2_DEST, manager);
	
	manager.addSubflow(&flow1);
	manager.addSubflow(&flow2);

	std::cout << "manager subflows size" << manager.getSubflows().size() 
		<< std::endl;

	std::thread flow1SendThread(
		&SenderFlowManager::sendLoop, &flow1);
	//std::thread flow1RecieveThread(
	//	&SenderFlowManager::recieveAndProcessAcks, &flow1);
	//std::thread flow1LossDetectThread(
	//		&SenderFlowManager::checkForLosses, &flow1);

	flow1SendThread.join();
	//flow1RecieveThread.join();
}
