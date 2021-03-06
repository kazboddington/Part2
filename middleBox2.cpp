#include <unistd.h>
#include <mutex>
#include <thread>
#include <list>

#include "PacketReciever.h"
#include "PacketSender.h"
#include "TimerManager.h"
#include "packetStruct.h"

void sendPacket(Packet p, PacketSender &packetSender){
	packetSender.sendPacket(&p);
}

class MiddleBox{
	PacketReciever reciever;
	PacketSender sender;
	std::mutex bufferMux;
	float lossProb;
	std::list<Packet> buffer;
	int maxBufferSize;
	TimerManager timerManager;
	std::chrono::duration<double, std::milli> packetDelay;
	int timeToProcessPacket;
	std::thread managerThread;
public:
	MiddleBox(
			int sourcePort, 
			char*  destinationPort,
			float lossProbability,
		   	int bufferSize,
			int outputDelay,
			int packetProcessTime):
		reciever(sourcePort),
		sender("127.0.0.1", destinationPort),
		lossProb(lossProbability),
		maxBufferSize(bufferSize),
		packetDelay(outputDelay),
		timeToProcessPacket(packetProcessTime),
		managerThread(
				&TimerManager::manageTasks,
				std::ref(timerManager)){

		std::cout << "Listening on port " << sourcePort << std::endl; 
		std::cout << "Sending on port " << destinationPort << std::endl; 

	}

	~MiddleBox(){
		managerThread.join();
	}	

	// constantly loop unloading a packet from the buffer every X length of time
	void processingLoop(){
		std::cout << "started processing" << std::endl;
		while(true){
			usleep(timeToProcessPacket*1000); //make it milliseconds
			bufferMux.lock();
			if(buffer.size() > 0){

				std::function<void()> resendPacket 
					= std::bind(sendPacket, buffer.front(), std::ref(sender));
				timerManager.addTask(packetDelay, resendPacket);

				std::cout << "Packet sent, seqNum = " << buffer.front().seqNum
					<< " " << buffer.size()-1 << "/" << maxBufferSize 
					<< "Packets in buffer" << std::endl;

				buffer.pop_front();
			}

			bufferMux.unlock();
		}
	}

	// Listen for packets constantly, whether putting them into the buffer, or 
	// dropping them if the buffer is full
	void recievingLoop(){
		std::cout << "Started recieving" << std::endl;
		while(true){
			Packet p = reciever.listenOnce();
			std::cout << "packet recieved..." << std::endl;

			if(lossProb == 0 || rand() % (int)(1/lossProb) != 0){
				bufferMux.lock();

				if((int)buffer.size() >= maxBufferSize){
					std::cout << "Dropping packet since buffer is full"
						<< std::endl;
				}else{
					buffer.push_back(p);
				}

				bufferMux.unlock();
			}else{
				std::cout << "Packet lose due to random loss" << std::endl;
			}
		}
	}

};


int main(int argc, char* argv[]){

	srand(time(NULL));

	if (argc != 7){
		std::cout << "Usage middlebox [IN_PORT] [OUT_PORT] "
			<< "[LOSS_PROB]TODO [PROCESSING_DELAY] [LINK_DELAY(ms)] " 
			<< "[BUFFERSIZE]" << std::endl;	
		return 0;
	}
	
	char* sourcePort = argv[1];
	char* destinationPort = argv[2];
	char* lossProbability = argv[3];
	char* processingDelay = argv[4]; // in ms
	char* linkDelay = argv[5];
	char* bufferSize = argv[6];

	MiddleBox box1(
			atoi(sourcePort), 
			destinationPort, 
			atof(lossProbability),
			atoi(bufferSize), 
			atoi(linkDelay),
			atoi(processingDelay));
	
	std::thread box1ProcessingThread(
			&MiddleBox::processingLoop,
			std::ref(box1));
	box1.recievingLoop();

	box1ProcessingThread.join();

	return 0;

}
