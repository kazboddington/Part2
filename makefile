.SUFFIXES: .o .cpp .h
SRCS = main.o PacketReciever.o PacketSender.o PacketSender.h PacketReciever.h

all:

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

