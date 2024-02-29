#Makefile for CMPUT379 Assignment 2
#
#

###Assignment 2 Part 1
a2p1: a2p1.c
	gcc -Wall -std=c99 -pthread ./a2p1.c -o a2p1

#a2p1r: create a2p1 executable and run with preset settings:
a2p1r: a2p1.c
	gcc -Wall -std=c99 -pthread ./a2p1.c -o a2p1 && ./a2p1 10 myFile 5432

#a2p1db: create gdb debug executable
a2p1db: a2p1.c
	gcc -Wall -std=c99 -pthread -ggdb ./a2p1.c -o a2p1db

###Assignment 2 Part 2
fifos:
	mkfifo fifo-0-1
	mkfifo fifo-1-0
a2p2: a2p2.c
	gcc -Wall ./a2p2.c -o a2p2
a2p2db: a2p2.c
	gcc -Wall -ggdb ./a2p2.c -o a2p2

#a2p2s: build and run executable as "server":
a2p2s: a2p2.c
	gcc -Wall ./a2p2.c -o a2p2 && ./a2p2 -s

#a2p2s: build and run executable as "client":
a2p2c: a2p2.c
	gcc -Wall ./a2p2.c -o a2p2 && ./a2p2 -c a2p2-w24-ex1.dat

#a2p2cdb: build and run executable as "client" with debug info:
a2p2cdb: a2p2.c
	gcc -Wall -ggdb ./a2p2.c -o a2p2 && gdb ./a2p2

#clean: remove debug executables
clean:
	rm ./a2p1db ./a2p2db
