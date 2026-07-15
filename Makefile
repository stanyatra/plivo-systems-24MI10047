all: sender receiver

sender: sender.c
	cc -O2 -Wall -o sender sender.c

receiver: receiver.c
	cc -O2 -Wall -o receiver receiver.c -lpthread

clean:
	rm -f sender receiver