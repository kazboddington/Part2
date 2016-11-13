.SUFFIXES: .o .cpp .h
SRCS = main.o PacketReciever.o PacketSender.o PacketSender.h PacketReciever.h

#.cpp.o:
#	gcc $< -c -o $@
#
all:
	g++ -std=c++11 -pthread -o sender main.cpp PacketSender.cpp PacketReciever.cpp

#depend: .depend
#
#.depend: $(SRCS)
#	rm -f ./.depend
#	g++ -MM $^ -MF ./.depend;
#
#include .depend
