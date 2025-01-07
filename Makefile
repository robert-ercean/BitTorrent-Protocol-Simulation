CC = mpic++ -g
FLAGS = -Wall

build: tema2

tema2: main.o peer.o tracker.o
	$(CC) $^ -o $@ $(FLAGS)

main.o: main.cpp utils.h
	$(CC) -c $< $(FLAGS)

peer.o: peer.cpp peer.h utils.h
	$(CC) -c $< $(FLAGS)

tracker.o: tracker.cpp tracker.h utils.h
	$(CC) -c $< $(FLAGS)

clean:
	rm -rf tema2 main.o peer.o tracker.o

