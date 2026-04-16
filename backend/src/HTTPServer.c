#include "HTTPServer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
// ================= DATA TYPES =================

struct ClientServer {
    int client;
    struct HTTPServer *server;
};

struct Route {
    int methods[9];
    char *uri;
    char * (*route_function)(struct HTTPServer *, struct HTTPRequest *);
};

// ================= DECL =================

void launch(struct HTTPServer *server);
void *handler(void *arg);
void register_routes(struct HTTPServer *server,
    char * (*route_function)(struct HTTPServer *, struct HTTPRequest *),
    char *uri, int num_methods, ...);

// ================= CONSTRUCTOR =================

struct HTTPServer http_server_constructor()
{
    struct HTTPServer server;

    server.server = server_constructor(AF_INET, SOCK_STREAM, 0, INADDR_ANY, 8085, 255);

    server.routes = dictionary_constructor(compare_string_keys);
    server.register_routes = register_routes;
    server.launch = launch;

    return server;
}

// ================= FIXED ROUTES =================

void register_routes(struct HTTPServer *server,
    char * (*route_function)(struct HTTPServer *, struct HTTPRequest *),
    char *uri, int num_methods, ...)
{
    struct Route *route = malloc(sizeof(struct Route));

    // copy methods
    va_list methods;
    va_start(methods, num_methods);
    for (int i = 0; i < num_methods; i++) {
        route->methods[i] = va_arg(methods, int);
    }
    va_end(methods);

    // ✅ FIX: heap allocation
    route->uri = malloc(strlen(uri) + 1);
    strcpy(route->uri, uri);

    route->route_function = route_function;

    // ✅ FIX: correct sizes
    server->routes.insert(
        &server->routes,
        route->uri,
        strlen(route->uri) + 1,
        route,
        sizeof(struct Route)
    );
}

// ================= LAUNCH =================

void launch(struct HTTPServer *server)
{
    struct sockaddr *sock_addr = (struct sockaddr *)&server->server.address;
    socklen_t address_length = sizeof(server->server.address);

    while (1)
    {
        struct ClientServer *client_server = malloc(sizeof(struct ClientServer));

        client_server->client = accept(server->server.socket, sock_addr, &address_length);
        client_server->server = server;

        // ✅ NO THREADS (debug mode)
        handler(client_server);
    }
}

// ================= HANDLER =================

void *handler(void *arg)
{
    struct ClientServer *client_server = (struct ClientServer *)arg;

    char request_string[30000] = {0};
    read(client_server->client, request_string, 30000);

    struct HTTPRequest request = http_request_constructor(request_string);

    char *uri = request.request_line.search(&request.request_line, "uri", sizeof("uri"));

    printf("Request: %s\n", uri);

    struct Route *route =
        client_server->server->routes.search(
            &client_server->server->routes,
            uri,
            strlen(uri) + 1
        );

    if (!route) {
        char *msg = "HTTP/1.1 404 Not Found\r\n\r\nNot Found";
        write(client_server->client, msg, strlen(msg));
        close(client_server->client);
        free(client_server);
        return NULL;
    }

    char *response = route->route_function(client_server->server, &request);

    write(client_server->client, response, strlen(response));

    close(client_server->client);
    free(client_server);

    http_request_destructor(&request);

    return NULL;
}