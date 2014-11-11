P1: main.cpp
	g++ -o P1 -Wall -g -std=c++0x -lpthread -DANSI main.cpp
clean:
	rm -rf *.o P1
