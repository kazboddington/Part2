#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <iostream>
#include <stdio.h>
#include <netdb.h>
#include <string.h>

#define PORT 9000

int main (){
	struct sockaddr_in myaddr;
	struct sockaddr_in remoteaddr;
	socklen_t addrlen = sizeof(remoteaddr);
	int recvlen;
	unsigned char buf[2000];	

	int fd;
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
		std::cout << "Failed to create socket: " << errno << std::endl;
		exit(0);	
	}
	
	memset((char*)&myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	myaddr.sin_port = htons(PORT);

	if(bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0){
		std::cout << "Failed to bind" << errno << std::endl;
		exit(0);
	}

	for(;;){
		std::cout << "Listeneing on port" << PORT << std::endl;
		recvlen = recvfrom(fd, buf, 2000, 0, (struct sockaddr *)&remoteaddr, &addrlen);
		std::cout << "Recieved " << recvlen	<< " bytes" << std::endl; 
	}

	return 1;
}
