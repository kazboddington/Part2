#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <iostream>
#include <stdio.h>
#include <netdb.h>
#include <string.h>

#include "PacketReciever.h"

// Used to recieve UDP packets from a given port
PacketReciever::PacketReciever(int p): port(p){
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
		std::cout << "Failed to create socket: " << errno << std::endl;
		exit(0);	
	}
	
	memset((char*)&myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	myaddr.sin_port = htons(port);

	if(bind(sockfd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0){
		std::cout << "Failed to bind" << errno << std::endl;
		exit(0);
	}
}	

void PacketReciever::startListening(){
	for(;;){
		std::cout << "Listening on port " << port << std::endl;
		recvlen = recvfrom(sockfd, buf, 2000, 0, (struct sockaddr *)&remoteaddr, &addrlen);
		std::cout << "Recieved " << recvlen	<< " bytes" << std::endl; 
		std::cout << "Data: " << buf << std::endl;
	}
}

/*	
int main (){
	PacketReciever reciever(9000);
	reciever.startListening();
	return 1;
}
*/
