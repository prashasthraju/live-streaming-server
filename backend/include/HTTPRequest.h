#ifndef HTTPRequest_h
#define HTTPRequest_h

#include "DataStructures/Dictionary/Dictionary.h"

enum HTTPmethods
{
    GET,
    POST,
    PUT,
    HEAD,
    PATCH,
    DELETE,
    CONNECT,
    OPTIONS,
    TRACE
};

struct HTTPReqest
{
    int Method;
    char *URI;
    float HTTPVersion;
    struct Dictionary header_fields;
};

struct HTTPRequest http_request_constructor(char *request_string);

#endif
