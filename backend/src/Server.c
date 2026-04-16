#include "Server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

void register_routes_server(struct Server *server, char *(*route_function)(void *arg), char *path);

// ================= CONSTRUCTOR =================

struct Server server_constructor(int domain, int service, int protocol, u_long interface, int port, int backlog)
{
    struct Server server;

    server.domain = domain;
    server.service = service;
    server.protocol = protocol;
    server.interface = interface;
    server.port = port;
    server.backlog = backlog;

    server.address.sin_family = domain;
    server.address.sin_port = htons(port);
    server.address.sin_addr.s_addr = htonl(interface);

    server.socket = socket(domain, service, protocol);

    server.routes = dictionary_constructor(compare_string_keys);
    server.register_routes = register_routes_server;

    if (server.socket == 0)
    {
        perror("socket failed");
        exit(1);
    }

    // ✅ FIX: reuse address (prevents restart errors)
    int opt = 1;
    setsockopt(server.socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server.socket, (struct sockaddr *)&server.address, sizeof(server.address)) < 0)
    {
        perror("bind failed");
        exit(1);
    }

    if (listen(server.socket, server.backlog) < 0)
    {
        perror("listen failed");
        exit(1);
    }

    return server;
}

// ================= ROUTES (FIXED) =================

void register_routes_server(struct Server *server, char *(*route_function)(void *arg), char *path)
{
    // ✅ FIX 1: allocate route on heap
    struct ServerRoute *route = malloc(sizeof(struct ServerRoute));

    route->route_function = route_function;

    // ✅ FIX 2: store key properly (strlen + 1)
    server->routes.insert(
        &server->routes,
        path,
        strlen(path) + 1,
        route,
        sizeof(struct ServerRoute)
    );
}