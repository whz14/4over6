cmpl:
	gcc main.cpp -std=c99 -pthread -o main
run:
	./main
all:
	gcc main.cpp -std=c99 -pthread -o main
	./main