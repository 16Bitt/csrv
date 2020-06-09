# csrv - C framework for http servers in the GoLang style

## Server interface

To create a server, initialize a `struct Csrv` and call `csrv_listen`. At high level, this
is what will be done:

- Create socket
- Bind socket
- Listen on socket
- Accept connections on socket
- Depending on the `csrv->model`, either fork, thread, or handle with an event queue
- Connections are then passed to the `CsrvRequest` interface

## Request interface

Once a connection is accepted, a `struct CsrvRequest` will be initialized, and the following
will happen:

- `read()` enough information to parse the headers of the request
- The headers will be set on the `req->headers.map` structure
    - Data from the first line, (`GET, POST, PATCH` etc) will also be set on `req->headers`

## Other data structures

- `struct CsrvStrVec`: This is a string vector (could also be viewed as a string builder)
- `struct CsrvStrMap`: This is a hashmap of string:string
