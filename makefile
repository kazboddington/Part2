.SUFFIXES: .o .cpp .h
SRCS = main.o PacketReciever.o PacketSender.o PacketSender.h PacketReciever.h

#.cpp.o:
#	gcc $< -c -o $@


all:

#	g++ -std=c++11 -pthread -o endpoint1 endpoint1.cpp PacketSender.cpp \
#		PacketReciever.cpp SenderThread.cpp TimerManager.cpp
#	g++ -std=c++11 -pthread -o endpoint2 endpoint2.cpp PacketSender.cpp \
#		PacketReciever.cpp SenderThread.cpp TimerManager.cpp

#	g++ -std=c++11 -pthread -o middlebox1 middlebox1.cpp PacketSender.cpp \
#		PacketReciever.cpp SenderThread.cpp TimerManager.cpp

	g++ -std=c++11 -pthread -o middleBox2 middleBox2.cpp PacketSender.cpp \
		PacketReciever.cpp SenderThread.cpp TimerManager.cpp

	g++ -std=c++11 -pthread -o sender2 sender2.cpp PacketSender.cpp \
	   	PacketReciever.cpp \
		-I/home/zak/dev/kodo-cpp/shared_test/include  \
		-I/home/zak/dev/kodo-cpp/shared_test/include/kodocpp \
		-L/home/zak/dev/kodo-cpp/shared_test \
		-Wl,-Bdynamic -lkodoc -Wl,-rpath /home/zak/dev/kodo-cpp/shared_test

	g++ -std=c++11 -pthread -o reciever2 reciever2.cpp PacketSender.cpp \
	   	PacketReciever.cpp \
		-I/home/zak/dev/kodo-cpp/shared_test/include  \
		-I/home/zak/dev/kodo-cpp/shared_test/include/kodocpp \
		-L/home/zak/dev/kodo-cpp/shared_test \
		-Wl,-Bdynamic -lkodoc -Wl,-rpath /home/zak/dev/kodo-cpp/shared_test

#	g++ test.cpp -o test -std=c++11 \
		-I/home/zak/dev/kodo-cpp/shared_test/include  \
		-I/home/zak/dev/kodo-cpp/shared_test/include/kodocpp \
		-L/home/zak/dev/kodo-cpp/shared_test \
		-Wl,-Bdynamic -lkodoc -Wl,-rpath /home/zak/dev/kodo-cpp/shared_test


#depend: .depend
#
#.depend: $(SRCS)
#	rm -f ./.depend
#	g++ -MM $^ -MF ./.depend;
#
#include .depend
