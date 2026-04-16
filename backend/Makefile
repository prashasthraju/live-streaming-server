CC = gcc
CFLAGS = -Iinclude

###############################################################################
# MARK: ALL
###############################################################################

all: Main

###############################################################################
# MARK: MAIN
###############################################################################

Main: objs
	ar rcs libeom.a $(OBJS)

###############################################################################
# MARK: OBJECT FILES
###############################################################################

OBJS = \
	Node.o LinkedList.o Queue.o BinarySearchTree.o Entry.o Dictionary.o \
	Client.o Server.o HTTPServer.o HTTPRequest.o PeerToPeer.o \
	ThreadPool.o Files.o

###############################################################################
# MARK: DATA STRUCTURES
###############################################################################

Node:
	$(CC) $(CFLAGS) -c DataStructures/Common/Node.c

LinkedList:
	$(CC) $(CFLAGS) -c DataStructures/Lists/LinkedList.c

Queue:
	$(CC) $(CFLAGS) -c DataStructures/Lists/Queue.c

BinarySearchTree:
	$(CC) $(CFLAGS) -c DataStructures/Trees/BinarySearchTree.c

Entry:
	$(CC) $(CFLAGS) -c DataStructures/Dictionary/Entry.c

Dictionary:
	$(CC) $(CFLAGS) -c DataStructures/Dictionary/Dictionary.c

###############################################################################
# MARK: NETWORKING (NOW FROM src/)
###############################################################################

Server:
	$(CC) $(CFLAGS) -c src/Server.c

Client:
	$(CC) $(CFLAGS) -c src/Client.c

PeerToPeer:
	$(CC) $(CFLAGS) -c src/PeerToPeer.c

HTTPServer:
	$(CC) $(CFLAGS) -c src/HTTPServer.c

HTTPRequest:
	$(CC) $(CFLAGS) -c src/HTTPRequest.c

###############################################################################
# MARK: SYSTEMS
###############################################################################

ThreadPool:
	$(CC) $(CFLAGS) -c Systems/ThreadPool.c

Files:
	$(CC) $(CFLAGS) -c Systems/Files.c

###############################################################################
# MARK: BUILD ALL OBJECTS
###############################################################################

objs: \
	Node LinkedList Queue BinarySearchTree Entry Dictionary \
	Server Client HTTPRequest HTTPServer PeerToPeer \
	ThreadPool Files

###############################################################################
# MARK: CLEAN
###############################################################################

clean:
	rm -f *.o libeom.a