#include <iostream>
#include <chrono>
#include <vector>
#include <thread> 

#include "PacketSender.h"
#include "PacketReciever.h"

#define FLOW1_DEST "4000"
#define FLOW2_DEST "5000"
#define FLOW1_SOURCE "2000"
#define FLOW2_SOURCE "3000"
#define PACKET_SIZE 1024
#define BLOCK_SIZE 10 // Defined in number of packets

// Forward declarations
class SenderFlowManager;

// Declaration of senderManager
class SenderManager{
private:
	std::vector<SenderFlowManager*> subflows;
public:
	void addSubflow(SenderFlowManager& subflow);
};

// Declaratin and implementatino of flowManager
class SenderFlowManager{
private:
	PacketSender sender;
	PacketReciever reciever;

	int seqNumNext = 0; // An index of the next packet to be sent
	int lastSeqNumAckd = 0; // The last seqence number to be acknowledged
	int currentBlock = 0; // The block in the priority position to be sent
	int tokens = 1; // The number of packets availble to be sent
	int DOFCurrentBlock = 1; // Degrees of freedon of current block

	SenderManager manager;
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




	/* The shared sender for this endpoint */

	SenderFlowManager(
		const char sourcePort[],
		const char destinationPort[],
		SenderManager theManager):
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

			auto packetRtt = std::chrono::high_resolution_clock::now()
			   - sentTimeStamps[p.seqNum]; 
			adjustRtt(packetRtt);
			
			lastSeqNumAckd = std::max(lastSeqNumAckd, (int)p.seqNum);
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
				
				int sent = expectedRecieved();
				Packet *p =
					calculatePacketToSend(sent, currentBlock, DOFCurrentBlock);

				// (2) Save timestamp and blocknumber 
				sentTimeStamps.push_back( 
					std::chrono::high_resolution_clock::now());
				seqNoToBlockMap.push_back(seqNumNext/BLOCK_SIZE); 
				

				// (3) Send packet, setting seqNum, blockNum etc.
				// TODO set blcknum, save blcoknum, seqNum accociation
				p->seqNum = seqNumNext;
				sender.sendPacket(p);
				std::cout << "Packet sent, seqNum= " << p->seqNum << std::endl;

				// (4) Clean up
				seqNumNext++;
				tokens--;
				delete p;
			}
		}
	}

	Packet* calculatePacketToSend(
			int sent,
			int currentBlock,
		   	int DOFCurrentBlock){
		// Creates packet on heap of the correct data to send 
		// TODO calculate which packet should be sent next and code it
		Packet *p = new Packet();
		return p;
	}

	int expectedRecieved(){
		// TODO Calculate the number of packets that we expect will be recieved
		// by the sender, given what we have currently sent
		return 1;
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
};

// Implementation of SenderManager
void SenderManager::addSubflow(SenderFlowManager &subflow){
	subflows.push_back(&subflow);
}


int main(){
	SenderManager manager;

	SenderFlowManager flow1 = 
		SenderFlowManager(FLOW1_SOURCE, FLOW1_DEST, manager);
	SenderFlowManager flow2 = 
		SenderFlowManager(FLOW2_SOURCE, FLOW2_DEST, manager);
	
	manager.addSubflow(flow1);
	manager.addSubflow(flow2);


	std::thread flow1SendThread(
		&SenderFlowManager::sendLoop, &flow1);
	std::thread flow1RecieveThread(
		&SenderFlowManager::recieveAndProcessAcks, &flow1);

	flow1SendThread.join();
	flow1RecieveThread.join();
}
