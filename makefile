cmpl:
	g++ main.cpp -std=c++11 -pthread -o main
run:
	./main
all:
	g++ main.cpp -std=c++11 -pthread -o main
	./main