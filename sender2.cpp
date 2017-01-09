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
	int setDataInBlock(int maxNumberOfPackets, std::vector<uint8_t>* dataToSet);
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
		uint32_t numberOfPacketsInFlight; 
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
			std::cout << "ACK RECIEVED seqNum = " << p.seqNum << std::endl;	

			// Adjust RTT
			auto packetRtt = std::chrono::high_resolution_clock::now()
			   - sentTimeStamps[p.seqNum]; 
			adjustRtt(packetRtt);

			// Adjust loss probability
			lossProbability = lossProbability*(1-alpha);

			// TODO check for timeouts	
			
			blockInfo* theBlock = mapSeqNumToBlockInfo[p.seqNum];

			theBlock->blockInfoMutex.lock();

			std::cout << "Block offset recieved from = " 
				<< theBlock->offsetInFile << std::endl
				<< "decrementing packets in flight from: " 
				<< theBlock->numberOfPacketsInFlight
				<< std::endl;

			--(theBlock->numberOfPacketsInFlight);
			theBlock->ackedDOF 
				= std::min(p.currentBlockDOF, theBlock->ackedDOF);
			std::cout << "p.currentBlockDOF " << p.currentBlockDOF 
				<< " theBlock->ackedDOF = " << theBlock->ackedDOF
				<< std::endl;
			std::cout << "currentBlockDOF set to: " << theBlock->ackedDOF
				<< std::endl;

			theBlock->blockInfoMutex.unlock();	

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
				std::cout << "Packet sent, seqNum = " << p->seqNum << std::endl;

				// Reduce number of tokens since a packet is sent 
				tokens--;

				// Clean up
				std::cout << "deleting packet..." << std::endl;
				delete p;
				std::cout << "done." << std::endl;
			}
		}
	}

	void checkForLosses(){
		// Loops, looking at the packets that are currently in flight,
		// checking to see if there are losses
		
		while(true){
			usleep(1000); // loop every 1 millisecond 
			int seqNum = lastSeqNumAckd + 1;			

			// No need to loop if we've already recieved the all acks 
			if(seqNum >= seqNumNext) continue;

			auto now = std::chrono::high_resolution_clock::now();

			// Loop thorough packets in flight, oldest to newest. Look for 
			// high-delay packets and count them as lost	
			// Note this doesn't use std deviation, which would be a better idea
			while(seqNum < seqNumNext &&
				rttInfo.average*10 < (now-sentTimeStamps[seqNum])){
				
				std::cout << "Loss Detected - seqNum = " << seqNum << std::endl;

				adjustForLostPacket(mapSeqNumToBlockInfo[seqNum]);
				
				// Update the lastSeqNumAcked as if packet is acked
				lastSeqNumAckd++;

				seqNum++;
			}
		}
	}
 
	void adjustForLostPacket(blockInfo* blockLostFrom){
		// Adjust the loss probability and the RTT, as well as increment tokens
		

		std::cout << "adjusting for lost packet in block: " 
			<< blockLostFrom->offsetInFile << " Packets in flight " 
			<< blockLostFrom->numberOfPacketsInFlight << std::endl;
		std::cout << " dataSize " << blockLostFrom->data.size() << std::endl;

		blockLostFrom->blockInfoMutex.lock();
		std::cout << "lock aquired" << std::endl;
		//blockLostFrom->numberOfPacketsInFlight -= 1;
		blockLostFrom->blockInfoMutex.unlock();
		lossProbability = lossProbability*(1-alpha)*(1-alpha) + alpha;
		std::cout << "Loss lossProbability = " << lossProbability << std::endl;
	}

	Packet* calculatePacketToSend(){
		// Creates packet on heap of the correct data to send 
		// TODO calculate which packet should be sent next and code it
		// NOTE, should set block number to correct value
			
		// NOTE: Assume that the encoder is set up correctly
		

		// Iterate through block infos until we find a block in which not enough
		// packets have been sent for the reciever to decode 

		std::list<blockInfo*>::iterator it;

		for(it = currentBlocksBeingSent.begin(); 
			it != currentBlocksBeingSent.end(); 
			++it){
			blockInfo* loopedBlock = *it;

			std::cout << "Grabbing block mutex for block at offset "
			   << loopedBlock->offsetInFile << std::endl;
			
			loopedBlock->blockInfoMutex.lock();

			std::cout << "Mutex aquired" << std::endl
				<< "Number of packets in flight: "
				<< loopedBlock->numberOfPacketsInFlight
				<< ", lossProbability = " << lossProbability
			   	<< ", DOF for this block: " << loopedBlock->ackedDOF
				<< std::endl;


			int expectedArrivals =
			   	loopedBlock->numberOfPacketsInFlight * lossProbability 
				+ loopedBlock->data.size() / PACKET_SIZE 
				- loopedBlock->ackedDOF;
			std::cout << "expectedArrivals = " << expectedArrivals << std::endl;

			if(expectedArrivals < ((int)loopedBlock->data.size()/PACKET_SIZE)){

				std::cout << "Generating packet from block at offset "
					<< loopedBlock->offsetInFile << " block Datasize = "
				   	<< loopedBlock->data.size() << std::endl; 

				loopedBlock->blockInfoMutex.unlock();
				Packet* p = generatePacketFromBlock(*it);
				return p;
			}
			loopedBlock->blockInfoMutex.unlock();
		}

		// None of the blocks in the list need packets sending. Create a new 
		// block and generate a packet using it.
		currentBlocksBeingSent.push_back(calculateNewBlock());
		blockInfo* theBlock = currentBlocksBeingSent.back();

		std::cout << "Created new block, offset = " 
			<< theBlock->offsetInFile << std::endl;

		Packet* p = generatePacketFromBlock(theBlock);

		return p;
	}
	Packet* generatePacketFromBlock(blockInfo* block){
		Packet *p = new Packet();
	
		// TODO decide a clearer place for this to go...		
		p->seqNum = seqNumNext;
		seqNumNext++;

		block->blockInfoMutex.lock(); // Take lock
		
		// Generate packet from encoder
		std::cout << "Writing the payload to the packet..." << std::endl;
	
		uint32_t bytesUsed = block->encoder.write_payload(p->data);
		std::cout << "Payload Size = " << bytesUsed << std::endl;

		p->dataSize = bytesUsed;
		std::cout << "dataSize = " << p->dataSize << std::endl;

		// Update number of packets in flight since we're sendin a packet
		std::cout << "Incrementing number of packets in flight...";
		(block->numberOfPacketsInFlight)++;
		std::cout << " value = " << block->numberOfPacketsInFlight << std::endl;

		// set the packt's offset field
		p->offsetInFile = block->offsetInFile;

		// Add the packet's seqnece number to the map of seqence numbers
		mapSeqNumToBlockInfo.push_back(block);
		std::cout << "put block: " << block->offsetInFile << " Into position" 
			<< mapSeqNumToBlockInfo.size() << std::endl;


		block->blockInfoMutex.unlock(); // release lock
		
		return p;		
	}
// TODO NOTE THAT THE TIMEOUT FOR LOSSES SEEMS TO CAUSE SEG FAULTS

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
	
	blockInfo* calculateNewBlock(){
		blockInfo* theBlock = new blockInfo();

		// Temporarily fixed length blocks
		uint32_t maxNumberOfPacketsInBlock = 250;

		// Grab data from manager
		uint32_t offset = manager.setDataInBlock(
				maxNumberOfPacketsInBlock, 
				&(theBlock->data));

		uint32_t numberOfPacketsInBlock = theBlock->data.size() / PACKET_SIZE;
		std::cout << "Got data to send, size = " 
			<< theBlock->data.size() << std::endl;
		std::cout << " looks like: " << theBlock->data.data() << std::endl;
		// TODO set offset in blockInfo

		// Create and encoder and accociate it with the data
		kodocpp::encoder_factory encoder_factory(
			kodocpp::codec::full_vector,
			kodocpp::field::binary8,
			numberOfPacketsInBlock,
			PACKET_SIZE);

		std::cout << "Building encoder..." << std::endl;
		theBlock->encoder = encoder_factory.build();

		std::cout << "Binding data to encoder: maxDataSize = "
		   << theBlock->data.size()	<< std::endl;

		std::cout << "Block Size = " << theBlock->encoder.block_size() 
			<< std::endl;
		theBlock->encoder.set_const_symbols(
				theBlock->data.data(),
				theBlock->data.size());

		theBlock->offsetInFile = offset;

		std::cout << "Setting number of DOF in the block" << std::endl;
		theBlock->ackedDOF = numberOfPacketsInBlock;
		
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

int SenderManager::setDataInBlock(
		int maxNumberOfPackets,
	   	std::vector<uint8_t>* dataToSet){

	int amountToSend = std::min(
			(int)(dataToSend.size() - sentUpTo),
			(int)maxNumberOfPackets*PACKET_SIZE);
	if(amountToSend <= 0) {
		std::cout << "ALL DATA SENT... amountToSend = " 
			<< amountToSend << std::endl;
		while(true);
	}
	std::cout << "Handing over " << amountToSend 
		<< " bytes to flow" << std::endl;

	*dataToSet = std::vector<uint8_t>(
			dataToSend.begin() + sentUpTo,
			dataToSend.begin() + sentUpTo + amountToSend);
	
	sentUpTo += amountToSend;

	return sentUpTo - amountToSend;
}


int main(){
	srand(time(NULL));
	std::vector<uint8_t> dataToSend(1000*PACKET_SIZE);
	std::generate(dataToSend.begin(), dataToSend.end(), rand);
	dataToSend[0] = 0; dataToSend[1] = 0; dataToSend[2] = 0;

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
	std::thread flow1RecieveThread(
		&SenderFlowManager::recieveAndProcessAcks, &flow1);
	std::thread flow1LossDetectThread(
			&SenderFlowManager::checkForLosses, &flow1);

	flow1SendThread.join();
	flow1RecieveThread.join();
}
