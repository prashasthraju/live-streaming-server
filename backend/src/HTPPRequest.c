#include "HTTPRequest.h"
#include "DataStructures/Lists/Queue.h"
#include <string.h>
#include <stdlib.h>

int method_select(char *method)
{
    if(strcmp(method, "GET") == 0)
    {
        return  GET;
    }
    else if(strcmp(method, "POST") == 0)
    {
        return POST;
    }
    else if(strcmp(method, "PUT") == 0)
    {
        return PUT;
    }
    else if(strcmp(method, "HEAD") == 0)
    {
        return HEAD;
    }
    else if(strcmp(method, "PATCH") == 0)
    {
        return PATCH;
    }
    else if(strcmp(method, "DELETE") == 0)
    {
        return DELETE;
    }
    else if(strcmp(method, "CONNECT") == 0)
    {
        return CONNECT;
    }
    else if(strcmp(method, "OPTIONS") == 0)
    {
        return OPTIONS;
    }else if(strcmp(method, "TRACE") == 0)
    {
        return TRACE;
    }   
    else
    {
        return -1;
    }
}

struct HTTPRequest http_request_constructor(char *request_string)
{   
    struct HTTPRequest request;
    char requested[strlen(request_string)]; //beacuse the parameter passed will become string literal and it is immutable
    strcpy(requested, request_string);
    for(int i=0; i<strlen(request_string)-1;i++)
    {
        if(reuquested[i] == '\n' && requested[i+1] == '\n')
        {
            printf("a/n");
            requested[i+1]='|'; // to separate header fields and the body
        }
    }
    char *request_line = strtok(requested,"\n");   
    char *header_fields = strtok(NULL,"|");
    char *body = strtok(NULL,"|");c
    
    char *method = strtok(req_line," ");
    request.Method = method_select(method);
    char *URI = strtok(NULL," ");
    request.URI = URI;
    char *HTTPVersion = strtok(NULL," "); // HTTP/1.1
    HTTPVersion = strtok(HTTPVersion,"/"); // /1.1
    HTTPVersion = strtok(NULL,"/"); // 1.1
    request.HTTPVersion = (float)atof(HTTPVersion);
    
    request.header_fields = dictionary_constructor(compare_string_keys);
    struct Queue headers = queue_constructor();
    char *token = strtok(header_fields,"\n");
    while (token)
    {
        headers.push(&headers, token, sizeof(*token));
        token = strtok(NULL,"\n");
    }
    char *header = (char *)headers.peek(&headers);
    while (header)
    {
        char *key = strtok(header, ":");
        char *value = strtok(NULL,"|");
        request.header_fields.insert(&request.header_fields, key, sizeof(key), value, sizeof(*value));
        headers.pop(&headers);
        header = (char *)headers.peek(&headers);
    }
    return request;
}
