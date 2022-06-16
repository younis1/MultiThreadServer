# MultiThreadServer

This repository a server.c program. The server connects to port, and accepts "client GET requests" (Only GET requests), and responds with the the item specified in request if found (200 status), else responds with a 404 status if item not found. 

The server is multi-threaded, so several clients can connect simultaneously.

To run server: 
1- Compile: "make" 
2- Run: ./server [4 if IPv4, 6 if IPv6] [port number, E.g. 8888] [path to web root]

To run client:
1. nc -c [IP Address of server device] [port number]
2. Send request: GET /folder1/a.html HTTP/1.1

Note that for request to be finalised, press enter twice. 

