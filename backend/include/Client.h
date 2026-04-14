#ifndef Client_h
#define Client_h

#include <sys/socket.h>
#include <netinet/in.h>

// MARK: DATA TYPES

struct Client
{
    /* PUBLIC MEMBER VARIABLES */
    // The network socket for handling connections.
    int socket;
    // Variables dealing with the specifics of a connection.
    int domain;
    int service;
    int protocol;
    int port;
    u_long interface;
    /* PUBLIC MEMBER METHODS */
    // The request method allows a client to make a request of a specified server.
    char * (*request)(struct Client *client, char *server_ip, void *request, unsigned long size);
};


// MARK: CONSTRUCTORS

struct Client client_constructor(int domain, int service, int protocol, int port, u_long interface);


#endif /* Client_h */
