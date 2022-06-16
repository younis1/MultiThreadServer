#define _POSIX_C_SOURCE 200112L
#define MAX_CONNECTIONS 5
#define MAX_FILE_SIZE 2049
#define HTTP_200_MSG "HTTP/1.0 200 OK\r\n"
#define LENGTH_OF_200 18
#define HTTP_404_MSG "HTTP/1.0 404 Not Found\r\n\r\n"
#define LENGTH_OF_404 26
#define BUFFER_SIZE 2500
#define MAX_THREADS 10
#define MULTITHREADED
#define IMPLEMENTS_IPV6

#include <assert.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>

typedef struct {
    int  client_socket;
    char* file_path;
} thread_struct;

/* Code used and inspired by the server.c in week9,
as well as youtube video "How to write a multithreaded server in C (threads, sockets)"
and stack overflow - sending file to client */


void check(int x, int min);  // Exit(EXIT_FAILURE) if x < min
void * respond(void * thread_args); // Where all the logic is implemented (The thread function)
void fill(char * array, int start, int length, char * second_array); // Copies elements of second array into first array
void check_content_type(char * type, char * complete_path, int complete_path_len);  // Copies the "content-type: ..." into "type"

int main(int argc, char** argv) {
    int sockfd, re, s;
    struct addrinfo hints, *res;
    struct sockaddr_storage client_addr;
    socklen_t client_addr_size;

    if (argc < 4) {
        fprintf(stderr, "Wrong input\n");
        exit(EXIT_FAILURE);
    }

    // Create address we're going to listen on (with given port number)
    if (strlen(argv[1]) != 1 || (argv[1][0]!= '4' && argv[1][0] != '6')){
        fprintf(stderr, "Wrong input\n");
        exit(EXIT_FAILURE);
    }


    memset(&hints, 0, sizeof hints);

    if (argv[1][0] == '4'){
        //IPv4 has been chosen

        hints.ai_family = AF_INET;       // IPv4
        hints.ai_socktype = SOCK_STREAM; // TCP
        hints.ai_flags = AI_PASSIVE;
        s = getaddrinfo(NULL, argv[2], &hints, &res);
        if (s < 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
            exit(EXIT_FAILURE);
        }
         // Create socket
        sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sockfd < 0) {
            perror("socket");
            exit(EXIT_FAILURE);
        }
            // Reuse port if possible
    }

    else{
        // IPv6
        hints.ai_family = AF_INET6;       // IPv6
        hints.ai_socktype = SOCK_STREAM; // TCP
        hints.ai_flags = AI_PASSIVE; 

        // used from lectures
        s = getaddrinfo(NULL, argv[2], &hints, &res);
        if (s < 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
            exit(EXIT_FAILURE);
        }
        for (struct addrinfo * p = res; p!= NULL; p = p->ai_next) {
            if (p->ai_family == AF_INET6
                    && (sockfd = socket(p->ai_family,
                                        p->ai_socktype,
                                        p->ai_protocol)) >= 0 )
            {
                // socket found
                break;
            }
        }
    }
       
    
    

   
    re = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &re, sizeof(int)) < 0) {
            perror("setsockopt");
            exit(EXIT_FAILURE);
    }
    // Bind address to the socket
    if (bind(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
            perror("bind");
            exit(EXIT_FAILURE);
    }
    freeaddrinfo(res);

    // Listen on socket - means we're ready to accept connections,
    // incoming connection requests will be queued
    if (listen(sockfd, MAX_CONNECTIONS) < 0) {
            perror("listen");
            exit(EXIT_FAILURE);
    }

    // mallocing all thread_structs variables
    thread_struct * all_thread_args[MAX_THREADS];
    for (int i =0; i < MAX_THREADS; i++){
        all_thread_args[i] = (thread_struct*) malloc(sizeof(thread_struct));
        assert(all_thread_args[i]);
    }
    // nc -C localhost [port]
    // Accept a connection - blocks until a connection is ready to be accepted
    // Get back a new file descriptor to communicate on
    int thread_counter = 0;
    while (1){
        
        // Accept connection into a created thread ... thread_counter incremented after that
        client_addr_size = sizeof client_addr;
        pthread_t new_thread;
        thread_struct  *thread_args = all_thread_args[thread_counter];
        
        // Accept
        thread_args->client_socket = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_size);
        if (thread_args->client_socket < 0) {
            perror("accept");
        }
        else{
            thread_args->file_path = argv[3];
            pthread_create(&new_thread, NULL, respond, thread_args);
            thread_counter += 1;
        }
    }

    // Not run if server is running ... just in case .. free all malloced thread variables
    for (int i=0; i< thread_counter; i++){
        free(all_thread_args[i]);
    }

    return 0;
}


void * respond(void * tempThreadArg){
    
    // This function is responsible for the HTTP status and Content type
    // First, reads client's request (assuming that it is a multi-packet)
    // Then, send 404 status if file cannot be opened, else send 200 status with content type and content 
    
    thread_struct thread_args = *((thread_struct *) tempThreadArg);
    char request[BUFFER_SIZE];
    int msg_len = 0;
    int n; 
    
    // reading multi-packet request
    while ((n = read(thread_args.client_socket, msg_len + request, sizeof(request) - msg_len))){
        
        if (n < 0) {
            exit(EXIT_FAILURE);
        }
        msg_len += n;
        if (msg_len > BUFFER_SIZE -1 || (request[msg_len -1] == '\n' &&
        request[msg_len -3] == '\n' && request[msg_len -2] == '\r' &&
        request[msg_len -4] == '\r')){ 
            break;}
    }

    request[msg_len] = '\0';

    // path variables
    int root_directory_len = strlen(thread_args.file_path);
    char subpath[BUFFER_SIZE];
    char complete_path[BUFFER_SIZE +root_directory_len];

    // Sending file variables
    int file_desc;
    int remaining_data; 
    int sent_bytes = 0;
    struct stat file_stat;
    long offset = 0; 


    char http_type[8];  // Stores the "HTTP/1.0"
    char type[BUFFER_SIZE]; // Stores the content-type: ..."

    // Reading request and storing path and http_version
    // ends connection if lower args are provided
    if(sscanf(request, "GET %s %s", subpath, http_type) < 2){
        close(thread_args.client_socket);
        return NULL;
    }
    
    // Concantenates root and sub_path into 'complete path'
    fill(complete_path, 0, root_directory_len, thread_args.file_path);
    fill(complete_path, root_directory_len, strlen(subpath), subpath);

    int complete_path_len = root_directory_len + strlen(subpath);


    // Check if " ../ " exists within path ... returns 404 if so
    if (strstr(complete_path, "/../") || (strlen(complete_path) >= 3 && complete_path[0] == '.' &&
     complete_path[1] == '.' && complete_path[2] == '/')){
        check(write(thread_args.client_socket, HTTP_404_MSG,  LENGTH_OF_404),0);
    }

    // Tries to open file, if file exists, returns 200 status, content type in a header
    // then writes the contents of the file
    // If file does not exist, then send a 404 and end the connection

    file_desc = open(complete_path, O_RDONLY);
    if (file_desc == -1){
        // file cannot be opened
        write(thread_args.client_socket, HTTP_404_MSG,  LENGTH_OF_404);
    }
    
    else{

        // File opened
        // Collect stats (used when sending the contents of file)
        if (fstat(file_desc, &file_stat) < 0)
        {
            exit(EXIT_FAILURE);
        }

        // Send a 200 status
        check(write(thread_args.client_socket, HTTP_200_MSG,  LENGTH_OF_200),0);

        // Get content type
        check_content_type(type, complete_path, complete_path_len);

        // Send content type and end header
        check(write(thread_args.client_socket, type,  strlen(type)),0);

        // Send the contents of the file (This part is used from a stack overflow - sending a packet)
        remaining_data =  file_stat.st_size;
        while (((sent_bytes = sendfile(thread_args.client_socket, file_desc, &offset, BUFSIZ)) > 0) && (remaining_data > 0))
        {
             remaining_data -= sent_bytes;
        } 
    }
    close(thread_args.client_socket);
    return NULL;

}
    
void fill(char * array, int start, int length, char * second_array){
    // copies the first 'length' elements of 'second_array'
    // into first array, starting from 'start' index
    for (int i=start; i < length + start; i++){
        array[i] = second_array[i-start];
    }
}

void check_content_type(char * type, char * complete_path, int complete_path_len){

    // Saves the content-type parametre inside "type"
    if (complete_path_len > 3 && !strcmp(complete_path + complete_path_len - 3, ".js")){
        sprintf(type, "Content-type: text/javascript\r\n\r\n");
    }

    else if(complete_path_len > 4 && !strcmp(complete_path + complete_path_len - 4, ".jpg")){
            sprintf(type, "Content-type: image/jpeg\r\n\r\n");  
    }
    else if(!strcmp(complete_path + complete_path_len - 4, ".css")){
            
            sprintf(type, "Content-type: text/css\r\n\r\n");  
    }
    else if(complete_path_len > 5 && !(strcmp(complete_path + complete_path_len - 5, ".html"))){
        sprintf(type, "Content-type: text/html\r\n\r\n"); 
    }
    else{
        sprintf(type, "Content-type: application/octet-stream\r\n\r\n");
    }
}

void check(int x, int min){
    // instead of so many if statements
    if (x < min){
        exit(EXIT_FAILURE);
    }
}

