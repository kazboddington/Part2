#ifndef PACKET_STRUCT
#define PACKET_STRUCT

enum Type {DATA, ACK}; 	

typedef struct Packet{
	enum Type type;
	unsigned int currentBlockDOF;
	unsigned short dataSize;
	unsigned int seqNum;
	unsigned int ackNum;
	unsigned int offsetInFile;
	unsigned short windowSize;
	unsigned char data[1500];	
}Packet;

/* type 1 is a standard data packet                      */
/* type 2 is a acknowledge packet                        */
/* sequence numbers are the byte number within the file. */

#endif
