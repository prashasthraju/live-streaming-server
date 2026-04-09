#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "Server.h"

void launch(struct Server *server)
{
    int address_length = sizeof(server->address);
    int new_socket;
    char *body = "<html><body><h1>Abcd Abcd</h1></body></html>";
    char response[1024];
    sprintf(response,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        strlen(body),
        body
    );
    while(1)
    {
        char buffer[30000] = {0};
        printf("==== WAITING FOR CONNECTION =====\n");
        new_socket = accept(server->socket, (struct sockaddr *)&server->address, (socklen_t *)&address_length);
        int bytes_read = read(new_socket, buffer, 30000);
        if (bytes_read < 0) {
            perror("read failed");
        }
        printf("%s\n", buffer);
        write(new_socket, response, strlen(response));
        close(new_socket);
    }
    
}

int main()
{
    struct Server server = server_constructor(AF_INET,SOCK_STREAM,0,INADDR_ANY,8080,10,launch);
    server.launch(&server);
}
