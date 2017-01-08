#include <iostream>
#include <chrono>
#include <vector>
#include <thread> 
#include <unistd.h>

#include "PacketSender.h"
#include "PacketReciever.h"

#define FLOW1_DEST "4000"
#define FLOW2_DEST "5000"
#define FLOW1_SOURCE "2000"
#define FLOW2_SOURCE "3000"
#define PACKET_SIZE 1024
#define BLOCK_SIZE 100 // Defined in number of packets

// Forward declarations
class SenderFlowManager;

// Declaration of senderManager
class SenderManager{
private:
	std::vector<SenderFlowManager*> subflows;
	int numberOfBlocks = 1000;
public:
	void addSubflow(SenderFlowManager *subflow);
	int getNumberOfBlocks();
	std::vector<int> getEstimatedPacketsPerBlock();
	std::vector<SenderFlowManager *> &getSubflows();
};

// Declaratin and implementatino of flowManager
class SenderFlowManager{
private:
	PacketSender sender;
	PacketReciever reciever;

	int seqNumNext = 0; // An index of the next packet to be sent
	int lastSeqNumAckd = 0; // The last seqence number to be acknowledged
	int currentBlock = 0; // The block in the priority position to be sent
	int tokens = 5; // The number of packets availble to be sent
	int DOFCurrentBlock = 1; // Degrees of freedon of current block

	double lossProbability = 0;
	SenderManager &manager;
	std::vector<std::chrono::high_resolution_clock::time_point> sentTimeStamps;
	std::vector<int> seqNoToBlockMap;
	
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

			// TODO check for timeouts	
			
			// Adjust  currentBlock, lastSeqNumAcked, currentDOF
			currentBlock = std::max(currentBlock, (int)p.blockNo);
			std::cout << "Current block: " << currentBlock << std::endl;
			lastSeqNumAckd = std::max(lastSeqNumAckd, (int)p.seqNum);
			DOFCurrentBlock = p.currentBlockDOF;

			// TODO adjust currBlock and number of degrees of freedom

			tokens++;
		}
	}

	void sendLoop(){
		// Naiive implementation - poll tokens, and send packet if token free
		while(true){
			if(tokens != 0){ // Allowed to send
				// (1) Calculate which packet is to be sent next
				// (2) Save timestamp in order to calculate RTT and save seqNum
				// and its accociated block
				// (3) Send packet
				// (4) Clean up remove token, increment seqNumNext, free packet

				// (1) Firstly need to calculate number of packets expected to 
				// be recieved by the reciever, given loss rates etc.
				
				std::vector<int> expectedToBeRecieved =
				   	manager.getEstimatedPacketsPerBlock();
				for(int block = 0; 
						block < ((int)expectedToBeRecieved.size()-1); block++){
					if (expectedToBeRecieved[block] >0){
						std::cout << "Block " << block << ": ";
						std::cout << expectedToBeRecieved[block] << std::endl;
					}
				}

				Packet *p = calculatePacketToSend();

				// (2) Save timestamp and blocknumber 
				sentTimeStamps.push_back( 
					std::chrono::high_resolution_clock::now());
				int blockNumber = p->blockNo;
				seqNoToBlockMap.push_back(blockNumber); 

				// (3) Send packet, setting seqNum, blockNum etc.
				// TODO set blcknum, save blcoknum, seqNum accociation
				p->seqNum = seqNumNext;
				sender.sendPacket(p);
				std::cout << "Packet sent, seqNum = " << p->seqNum;
				std::cout << " Block Number = " << blockNumber << std::endl;

				// (4) Clean up
				seqNumNext++;
				tokens--;
				delete p;
			}
		}
	}

	void checkForLosses(){
		// Loops, looking at the packets that are currently in flight,
		// checking  to see if there are losses
		
			int seqNum = lastSeqNumAckd + 1;			
		while(true){
			usleep(1000000);
			if(seqNum >= seqNumNext)
				continue;
			auto now = std::chrono::high_resolution_clock::now();
			std::cout << "Checking for loss... " << std::endl;
			std::cout << "seqNum = " << seqNum << " sentTimeStamps[seqNum] = " 
				<< sentTimeStamps[seqNum].time_since_epoch().count() 
				<< std::endl;
			std::cout << "now = " << now.time_since_epoch().count()
				<< std::endl;
			std::chrono::duration<double> durationOfPacket = 
				std::chrono::duration<double>(now-sentTimeStamps[seqNum]);
			std::cout << "Packet Duration = " << durationOfPacket.count()
				<< std::endl;
			std::cout << "RTTaverage*1.5 = " << (rttInfo.average.count()*1.5);
			while(seqNum <= seqNumNext &&
				rttInfo.average*1.5 < (now-sentTimeStamps[seqNum])){
				std::cout << "Loss Detected... " << std::endl;
				adjustForLostPacket(seqNum, sentTimeStamps[seqNum]);		
				lastSeqNumAckd++;
				seqNum++;
			}
		}
	}
 
	void adjustForLostPacket(
		int seqNum, std::chrono::high_resolution_clock::time_point sentTime){
		// Adjust the loss probability and the RTT, as well as increment tokens
		tokens++;
	}

	std::vector<int> calculateOnFlyPerBlock(){
		// calculate how many packets are on the fly for each block, taking
		// taking into account packets that have taken a long time to return;
		
		std::cout << "Calculating OnFlyPerBlock " << std::endl;
		std::vector<int> onFlyPerBlock(manager.getNumberOfBlocks());

		auto tooLongRtt = rttInfo.average*1.5;
		auto now = std::chrono::high_resolution_clock::now();
		for(int seqNum = lastSeqNumAckd; seqNum < (seqNumNext-1); seqNum++){
			if(tooLongRtt > (now - sentTimeStamps[seqNum])){
				onFlyPerBlock[seqNoToBlockMap[seqNum]]++;					
			}
		}		
		return onFlyPerBlock;
	}
	

	Packet* calculatePacketToSend(){
		// Creates packet on heap of the correct data to send 
		// TODO calculate which packet should be sent next and code it
		// NOTE, should set block number to correct value
		Packet *p = new Packet();
		p->blockNo = currentBlock;
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
	
	int getCurrentBlock(){
		return currentBlock;
	}
};

// Implementation of SenderManager
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

std::vector<int> SenderManager::getEstimatedPacketsPerBlock(){
	std::vector<int> estimatedPackets(getNumberOfBlocks());
	std::cout << "Estimating packets per block " << std::endl;
	std::cout << "size of subflows = " << subflows.size() << std::endl;
	// Loop through each flow and calculate the number of packets on the fly
	// For each, and add them to the total, adjusting for the estimated loss 
	// rate of each.
	for(SenderFlowManager *flow : subflows){
		std::vector<int> flowOnFly = (*flow).calculateOnFlyPerBlock();
		for (int blockNo = (*flow).getCurrentBlock();
			   	blockNo < getNumberOfBlocks(); blockNo++){
			estimatedPackets[blockNo] += 
				(1 - (*flow).getLossProbability())*flowOnFly[blockNo];
		}
	}
	return estimatedPackets;
}


int main(){
	SenderManager manager;

	SenderFlowManager flow1 = 
		SenderFlowManager(FLOW1_SOURCE, FLOW1_DEST, manager);
	SenderFlowManager flow2 = 
		SenderFlowManager(FLOW2_SOURCE, FLOW2_DEST, manager);
	
	manager.addSubflow(&flow1);
	manager.addSubflow(&flow2);

	std::cout << "manager subflows size" << manager.getSubflows().size();

	std::thread flow1SendThread(
		&SenderFlowManager::sendLoop, &flow1);
	std::thread flow1RecieveThread(
		&SenderFlowManager::recieveAndProcessAcks, &flow1);
	std::thread flow1LossDetectThread(
			&SenderFlowManager::checkForLosses, &flow1);

	flow1SendThread.join();
	flow1RecieveThread.join();
 }
