.SUFFIXES: .o .cpp
.cpp.o:
	g++ $< -o $@
all: main.o server.o
