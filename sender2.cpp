
#include <iostream>
#include <chrono>
#include <vector>
#include <thread> 
#include <unistd.h>
#include <algorithm>
#include <list>
#include <mutex>
#include <map>
#include <atomic>
#include <fstream>

#include <kodocpp/kodocpp.hpp>
#include "netcode/decoder.hh"
#include "PacketSender.h"
#include "PacketReciever.h"

#define FLOW1_DEST "4000"
#define FLOW2_DEST "5001"
#define FLOW3_DEST "2002"
#define FLOW1_SOURCE "2000"
#define FLOW2_SOURCE "3000"
#define FLOW3_SOURCE "2001"

#define PACKET_SIZE 1024


// Forward declarations
class SenderFlowManager;

// Declaration of senderManager
class SenderManager{
private:
	std::vector<SenderFlowManager*> subflows;
	std::vector<uint8_t>& dataToSend; 
public:
	int sentUpTo = 0;
	SenderManager(std::vector<uint8_t>& dataToSend);
	void addSubflow(SenderFlowManager *subflow);
	std::vector<SenderFlowManager *> &getSubflows();
	// Hands over a block of data for a flow to send to the reciever of the
	// asked for size. 
	int getDataSize();
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

	// The number of packets availble to be sent
	std::mutex tokensMux;
	volatile double tokens = 1; 
	volatile double tokensMax = 1;

  	const double alpha = 0.01; // Used for updating loss probability
	const double beta = 0.125; // as per original TCP

	std::mutex lossProbMux;
	volatile double lossProbability = 0;

	typedef struct blockInfo{
		std::mutex blockInfoMutex; // Must Lock this before acccessing info
		std::vector<uint8_t> data; // Data being sent, controlled by encoder
		uint32_t offsetInFile; // Where in the original file this data is from
		kodocpp::encoder encoder;
		volatile uint32_t ackedDOF;
		volatile uint32_t numberOfPacketsInFlight; 
	}blockInfo;

	std::list<blockInfo*> currentBlocksBeingSent; 
	// Info for each block beingg sent by this flow
	// Note that we delete blocks from this once they're sent
	
	std::vector<std::chrono::steady_clock::time_point> sentTimeStamps;
	std::vector<blockInfo*> mapSeqNumToBlockInfo;
	std::map<unsigned int, bool> finishedWithPacket;

	volatile bool isSlowStart = true;
	std::chrono::steady_clock::time_point tokenReducedTimestamp;

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

	void tokensMaxThreadPrinter(std::string fileName){
		std::ofstream myFile;
		myFile.open(fileName);
		auto startTime = std::chrono::steady_clock::now();	
		while(true){
			auto now = std::chrono::steady_clock::now();	
			auto delta = now - startTime;
			if(delta < std::chrono::duration<double>(20)){
				usleep(10000);	
					
				myFile <<
				   	std::chrono::duration<double>(delta).count()
					<< "\t" << tokensMax << "\n";
			}else{
				break;
			}
		}	
		myFile.close();
	}

	void recieveAndProcessAcks(){
		// Do three things here 
		// (1) Update RTT
		// (2) Check for timeouts
		// (3) Update lastSeqNumAckd, blockNumber, current DOF.
		// (4) Increament tokens
		while(true){
			Packet p = reciever.listenOnce();
			std::cout << std::endl <<"ACK RECIEVED seqNum = " 
				<< p.seqNum << std::endl;	

			// Adjust RTT
			auto packetRtt = std::chrono::steady_clock::now()
			   - sentTimeStamps[p.seqNum]; 

			// initialise rtt estimate
			if (p.seqNum == 0){
				rttInfo.average = packetRtt;
				rttInfo.deviataion = packetRtt/4; 
				rttInfo.min = packetRtt;
			}
			adjustRtt(packetRtt);

			// Check Packet is not already lost 
			// not in finishedWithPacket => not finished with yet 
			// not finished with
			if((finishedWithPacket.find(p.seqNum) == finishedWithPacket.end())
				   	|| !(finishedWithPacket[p.seqNum])){

				
				// Adjust loss probability
				lossProbMux.lock();
				lossProbability = lossProbability*(1-alpha);
				std::cout << "lossProbability = " << lossProbability
					<< std::endl;
				lossProbMux.unlock();

				blockInfo* theBlock = mapSeqNumToBlockInfo[p.seqNum];

				theBlock->blockInfoMutex.lock();

				std::cout << "Block offset recieved from = " 
					<< theBlock->offsetInFile << std::endl
					<< "decrementing packets in flight from: " 
					<< theBlock->numberOfPacketsInFlight
					<< std::endl;

				--(theBlock->numberOfPacketsInFlight);
				theBlock->ackedDOF 
					= std::min(p.currentBlockDOF, (uint32_t)theBlock->ackedDOF);
				
				std::cout << "p.currentBlockDOF " << p.currentBlockDOF 
					<< " theBlock->ackedDOF = " << theBlock->ackedDOF
					<< std::endl;
				std::cout << "currentBlockDOF set to: " << theBlock->ackedDOF
					<< std::endl;

				theBlock->blockInfoMutex.unlock();	

				// Adjust  lastSeqNumAcked, currentDOF
				lastSeqNumAckd = std::max(lastSeqNumAckd, (int)p.seqNum);

				tokensMux.lock();
				// Adjust window(tokens) appropriately
				if(isSlowStart){
					tokens += 0.5;
					tokensMax += 0.5;
				}else{
					
					tokens += (1/tokensMax)*
						(0.25 - (rttInfo.average-rttInfo.min)/rttInfo.min);
					tokensMax += (1/tokensMax)*
						(0.25 - (rttInfo.average-rttInfo.min)/rttInfo.min);
				}
				
				// Free up token since got ack
				tokens++;
				std::cout << "max tokens = " << tokensMax << " rttmin/rtt = " 
					<< rttInfo.min/rttInfo.average << " tokens = " << tokens 
					<< std::endl;
				std::cout << std::endl << "Incremented TOKENS to " 
					<< tokens << std::endl << std::endl;

				tokensMux.unlock();
			}
			
			finishedWithPacket[p.seqNum] = true;	
		}
	}

	void sendLoop(){
		// Naiive implementation - poll tokens, and send packet if token free
		while(true){
			tokensMux.lock();
			if(tokens >= 0){ // Allowed to send
				tokensMux.unlock();
				// Create packet to send, setting fields etc.
				try{
					// Try to generate packet. Can't do so if no more data if 
					// too many blocks in flight and all data has been alocated 
					// to blocks
					

					Packet *p = calculatePacketToSend();
					std::cout << "Packet calculated to send" << std::endl;

					// Save timestamp 
					sentTimeStamps.push_back( 
						std::chrono::steady_clock::now());

					// Send packet
					std::cout << " packet dataSize = " << p->dataSize << std::endl;
					sender.sendPacket(p);
					std::cout << "Packet sent, seqNum = " 
						<< p->seqNum << std::endl;

					// Reduce number of tokens since a packet is sent 
					tokensMux.lock();
					tokens--;
					tokensMux.unlock();

					// Clean up
					std::cout << "deleting packet..." << std::endl;
					delete p;
					std::cout << "done." << std::endl;
				}catch(int a){
					if(a == 1){
						std::cout << " No packet generated, since no more data"
						 	<< std::endl;
						if(currentBlocksBeingSent.back()->ackedDOF == 0){
							std::cout << "All data sent and acked" << std::endl;
							return;
						}
					}else{
					 	std::cout << " no idea what the error was: " << a
							<< std::endl;	
					}
				}
			}else{
				tokensMux.unlock();
			}
			
		}
	}

	void checkForLosses(){
		// Loops, looking at the packets that are currently in flight,
		// checking to see if there are losses
		
		while(true){
			usleep(1000); // loop every 1 millisecond 

			// Loop thorough packets in flight, oldest to newest. Look for 
			// high-delay packets and count them as lost	
			// Note this doesn't use std deviation, which would be a better idea

			int mostRecentTimeOut = 0; 
			// not strictly necesarry, just makes it faster
		
			// NOTE: seqNum-1 since otherwise we get reading memory that's not
			// always written, although this technically isn't correct	
			for(int seqNum = mostRecentTimeOut; seqNum < seqNumNext-1; seqNum++){
				
				if(finishedWithPacket.find(seqNum) == finishedWithPacket.end()
						|| !finishedWithPacket[seqNum]){
					auto now = std::chrono::steady_clock::now();
					
					if (now < sentTimeStamps[seqNum]){
						// Should never happen, but does if the OS 
						// ajusts for clock skew.
						std::cout << "Gone Back in time..."
							<< "SeqNum = " << seqNum << std::endl;
						continue;
					}

					std::chrono::duration<double> timeTaken = 
						(now-sentTimeStamps[seqNum]);

					// other possible method
					
					if(rttInfo.average + rttInfo.deviataion*4 < timeTaken){
					//if(rttInfo.average*2 < timeTaken){
						finishedWithPacket[seqNum] = true;
						mostRecentTimeOut = seqNum;

						std::cout << "Loss Detected - seqNum = " << seqNum 
							<< " SeqNumNext = " << seqNumNext 
							<< " lastSeqNumAckd " << lastSeqNumAckd
							<< " timeTaken = " << timeTaken.count() 
							<< " RttAverage + deviation*3 = " 
							<< (rttInfo.average + rttInfo.deviataion*3).count()
							<< " RTTAverage = " << rttInfo.average.count()
							<< " RTTDeviation =" << rttInfo.deviataion.count()
							<< " now = " << now.time_since_epoch().count()
							<< " timestamp = " 
							<< sentTimeStamps[seqNum].time_since_epoch().count()
							<< std::endl;
						
						adjustForLostPacket(
								mapSeqNumToBlockInfo[seqNum], 
								seqNum);
						
					}
				}
			}
		}
	}
 
	void adjustForLostPacket(blockInfo* blockLostFrom, int seqNum){
		// Adjust the loss probability and the RTT, as well as increment tokens
		

		std::cout << "adjusting for lost packet in block: ";
		std::cout << blockLostFrom->offsetInFile;
		std::cout << " seqNum = " << seqNum;
		std::cout << blockLostFrom->numberOfPacketsInFlight << std::endl;
		std::cout << " dataSize " << blockLostFrom->data.size() << std::endl;


		blockLostFrom->blockInfoMutex.lock();
		blockLostFrom->numberOfPacketsInFlight -= 1;

		// adjust congeston control
		tokensMux.lock();
		tokens++;
		tokensMux.unlock();

		//
		// Adjust for losses not in slow start mode, (only every 1RTT)
		//
		auto now = std::chrono::steady_clock::now();

		if((now - tokenReducedTimestamp) > rttInfo.average){
			tokenReducedTimestamp = now;
			if(isSlowStart){
				tokens -= tokensMax*(1 - rttInfo.min/rttInfo.average);
				std::cout << "Tokens Max reduced from : " << tokensMax;
				tokensMax -= tokensMax*(1 - rttInfo.min/rttInfo.average);
				isSlowStart = false;	
			}
			tokensMux.unlock();
		}



		blockLostFrom->blockInfoMutex.unlock();

		lossProbMux.lock();
		std::cout << "lossProbability was "	<< lossProbability << " now: " ;
		lossProbability = lossProbability*(1-alpha)*(1-alpha) + alpha;
		std::cout << "\tlossProbability = " << lossProbability << std::endl;
		lossProbMux.unlock();
	}

	Packet* calculatePacketToSend(){
		// Creates packet on heap of the correct data to send 
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

			int expectedArrivals =
			   	loopedBlock->numberOfPacketsInFlight * (1-lossProbability)
				+ loopedBlock->data.size() / PACKET_SIZE 
				- loopedBlock->ackedDOF;
			std::cout << "Block " << loopedBlock->offsetInFile 
				<<" expectedArrivals = " << expectedArrivals << std::endl;

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
		
		try{
			currentBlocksBeingSent.push_back(calculateNewBlock());
			blockInfo* theBlock = currentBlocksBeingSent.back();

			std::cout << "Created new block, offset = " 
				<< theBlock->offsetInFile << std::endl;

			Packet* p = generatePacketFromBlock(theBlock);
			return p;
		}catch(int a){
			throw;
		}
	}

	Packet* generatePacketFromBlock(blockInfo* block){
		Packet *p = new Packet();
		
		// Add the packet's seqnece number to the map of seqence numbers
		mapSeqNumToBlockInfo.push_back(block);
		std::cout << "put block: " << block->offsetInFile << " Into position" 
			<< mapSeqNumToBlockInfo.size() << std::endl;
	
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

		// Update number of packets in flight since we're sending a packet
		std::cout << "Incrementing number of packets in flight..." << std::endl;
		(block->numberOfPacketsInFlight)++;
		
		for(blockInfo* b : currentBlocksBeingSent){
			if(b->numberOfPacketsInFlight > 0){
			std::cout << "\t Block " << b->offsetInFile << " has " 
				<< b->numberOfPacketsInFlight << " packets in flight"
				<< std::endl;
			}
		}

		// set the packt's offset field
		p->offsetInFile = block->offsetInFile;

		block->blockInfoMutex.unlock(); // release lock
		
		return p;		
	}

	void adjustRtt(std::chrono::duration<double, std::micro> measurement){
		std::chrono::duration<double> error = measurement - rttInfo.average;
		rttInfo.average = rttInfo.average + beta*error;
		if(error.count() < 0) error = -error;
		rttInfo.deviataion += 2*beta*(error- rttInfo.deviataion);
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
		uint32_t maxNumberOfPacketsInBlock = 10;

		// Grab data from manager
		uint32_t offset;
		try{
			offset = manager.setDataInBlock(
					maxNumberOfPacketsInBlock, 
					&(theBlock->data));
		}catch(int a){
			throw;
		}

		uint32_t numberOfPacketsInBlock = theBlock->data.size() / PACKET_SIZE;
		std::cout << "Got data to send, size = " 
			<< theBlock->data.size() << std::endl;
		std::cout << " looks like: " << theBlock->data.data() << std::endl;

		// Create and encoder and accociate it with the data
		kodocpp::encoder_factory encoder_factory(
			kodocpp::codec::reed_solomon,
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
		std::cout << "All data allocated " 
			<< amountToSend << std::endl;
		throw 1;
	}

	std::cout << "Handing over " << amountToSend 
		<< " bytes to flow" << std::endl;

	*dataToSet = std::vector<uint8_t>(
			dataToSend.begin() + sentUpTo,
			dataToSend.begin() + sentUpTo + amountToSend);
	
	sentUpTo += amountToSend;

	return sentUpTo - amountToSend;
}

int SenderManager::getDataSize(){
	return dataToSend.size();
}

int main(){
	srand(time(NULL));

	// Create some random data to send	
	std::vector<uint8_t> dataToSend(1000*PACKET_SIZE);
	std::generate(dataToSend.begin(), dataToSend.end(), rand);
	dataToSend[0] = 0; dataToSend[1] = 0; dataToSend[2] = 0;

	// Create a manager to send the data
	SenderManager manager(dataToSend);

	SenderFlowManager flow1(FLOW1_SOURCE, FLOW1_DEST, manager);
	SenderFlowManager flow2(FLOW2_SOURCE, FLOW2_DEST, manager);
	SenderFlowManager flow3(FLOW3_SOURCE, FLOW3_DEST, manager);
	
	manager.addSubflow(&flow1);
	//manager.addSubflow(&flow2);
	//manager.addSubflow(&flow3);

	std::cout << "manager subflows size" << manager.getSubflows().size() 
		<< std::endl;

	std::thread maxTokensOutputThread(
			&SenderFlowManager::tokensMaxThreadPrinter,
			&flow1,
			"tokensMaxOutPut.cvv");

	std::thread flow1SendThread(
		&SenderFlowManager::sendLoop, &flow1);
	std::thread flow1RecieveThread(
		&SenderFlowManager::recieveAndProcessAcks, &flow1);
	std::thread flow1LossDetectThread(
			&SenderFlowManager::checkForLosses, &flow1);

//	std::thread flow2SendThread(
//		&SenderFlowManager::sendLoop, &flow2);
//	std::thread flow2RecieveThread(
//		&SenderFlowManager::recieveAndProcessAcks, &flow2);
//	std::thread flow2LossDetectThread(
//			&SenderFlowManager::checkForLosses, &flow2);
//
//	std::thread flow3SendThread(
//		&SenderFlowManager::sendLoop, &flow3);
//	std::thread flow3RecieveThread(
//		&SenderFlowManager::recieveAndProcessAcks, &flow3);
//	std::thread flow3LossDetectThread(
//			&SenderFlowManager::checkForLosses, &flow3);
//
	maxTokensOutputThread.join();

	flow1SendThread.join();
	flow1RecieveThread.join();

//	flow2SendThread.join();
//	flow2RecieveThread.join();
//	
//	flow3SendThread.join();
//	flow3RecieveThread.join();
}

