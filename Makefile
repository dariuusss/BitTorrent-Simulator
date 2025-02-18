build:
	mpic++ -o bittorrent main.cpp -pthread -Wall

clean:
	rm -rf bittorrent
