#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static const char *host_init = "Host: ";
static const char *accept_line_hdr = "Accept */* \r\n";
static const char *end_line = "\r\n";
static const char *default_port = "80";
static const char *version_hdr = " HTTP/1.0";
static const char *colon = ":";
void echo(int connfd);


int sfd;
struct sockaddr_in ip4addr;

void parse_host_and_port(char* request, char* host, char* port)
{
    char request_cpy[MAXBUF] = {0};
    strcpy(request_cpy, request);

    char *temp = strstr(request_cpy,"Host:");
    if (temp){
        temp = temp + 6;

        char* p = strchr(temp,':');
        char* n = strchr(temp, '\r');

        if (p < n){
            *p = '\0';
            strcpy(host, temp);
            char* temp_p = p + 1;
            p = strchr(temp_p,'\r');
            *p = '\0';
            strcpy(port, temp_p);
        }
        else {
            memcpy(port, default_port, strlen(default_port));
            *n = '\0';
            strcpy(host, temp);

        }
    }
    else {
        return;
    }
}

int parse_request(char* request, char* type, char* protocol, char* host, char* port, char* resource, char* version){
	char url[MAXBUF];
	if(strlen(request) == 0) {
        return -1;
    }

	if(!strstr(request, "/")){ 
		return -1;
    }

    parse_host_and_port(request, host, port);
	sscanf(request,"%s %s %s", type, url, version);

    char* test = strstr(url, "://");
	
	if (test) {
    	strcpy(resource, "/");
		sscanf(url, "%[^:]://%*[^/]%s", protocol, resource);
        if(!strcmp(resource, "/\0")){
            resource = "/\0";
        }

    }
	else {
    	strcpy(resource, "/");
		sscanf(url, "[^/]%s", resource);
    }

	return 0;
}

int read_bytes(int fd, char* p)
{
    int total_read = 0;
    ssize_t nread = 0;
    char temp_buf[MAXBUF] = {0};
    while(1) {
		nread = recv(fd, (p + total_read), MAXBUF, 0);
        total_read += nread;
        strcpy(temp_buf, p + total_read - 4);
		if (nread == -1) {
            fprintf(stdout, "Error opening file: %s\n", strerror( errno ));
			continue;     
        }         

        if (nread == 0){
            break;
        }
        
        if(!strcmp(temp_buf, "\r\n\r\n")){
            break;
        }
	}
    return total_read;
}

int create_send_socket(int sfd, char* port, char* host, char* request, int length, char* p)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s;


    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* TCP socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0;          /* Any protocol */
    s = getaddrinfo(host, port, &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype,
                    rp->ai_protocol);
        if (sfd == -1)
    continue;
        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;                  /* Success */
        close(sfd);
    }
    if (rp == NULL) {               /* No address succeeded */
        fprintf(stderr, "Could not connect\n");
    }
    freeaddrinfo(result);           /* No longer needed */

    ssize_t nread = 0;
    
    sleep(1);
    while (length > 0)
    {
        nread = write(sfd, request, length);

        if (nread <= 0)
            break;
        request += nread;
        length -= nread;
    }

    int total_read = 0;
    nread = 0;
    while(1) {
		nread = recv(sfd, (p + total_read), MAXBUF, 0);
        total_read += nread;
		if (nread == -1) {
            printf("error: %s\n", strerror(errno));
			continue;     
        }         

        if (nread == 0){
            break;
        }
    }
    sleep(1);
    return total_read;
}

void forward_bytes_to_client(int sfd, char* request_to_forward, int length)
{
    ssize_t nread;
    // printf("length = %d\n", length);
    while (length > 0)
    {
        nread = write(sfd, request_to_forward, length);
        if (nread <= 0){
            printf("error: %s\n", strerror(errno));
            break;
        }
        request_to_forward += nread;
        length -= nread;
    }
    return;
}

void send_request(int sfd, char* request, int length)
{
    ssize_t nread;

    while (length > 0)
    {
        nread = write(sfd, request, length);
        if (nread <= 0)
            break;
        request += nread;
        length -= nread;
    }
    return;
}

void *thread(void *vargp) 
{  
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self()); 
    char request[MAXBUF] = {0};
    char type[BUFSIZ] = {0};
    char host[BUFSIZ];
    char port[BUFSIZ];
    char resource[BUFSIZ] = {0};
    char protocal[BUFSIZ] = {0};
    char version[BUFSIZ] = {0};
    int sfd = 0;
    int req_val = 0;

    read_bytes(connfd,request);

    req_val = parse_request(request, type, protocal, host, port, resource, version);
    // printf("request = %s\n", request);
    // printf("type = %s\n", type);
    // printf("protocal = %s\n", protocal);
    // printf("host = %s\n", host);
    // printf("port = %s\n", port);
    // printf("resource = %s\n", resource);
    // printf("version = %s\n\n", version);

    if (req_val == 0){
        char new_request[MAXBUF] = {0};
        // char* p = new_request;

        strncat(new_request, type, BUFSIZ);
        strncat(new_request, " ", 2);
        strncat(new_request, resource, BUFSIZ);
        strncat(new_request, version_hdr, BUFSIZ);
        strncat(new_request, end_line,BUFSIZ);
        strncat(new_request, host_init, BUFSIZ);
        strncat(new_request, host, BUFSIZ);
        strncat(new_request, colon, BUFSIZ);
        strncat(new_request, port, BUFSIZ);
        strncat(new_request, end_line, BUFSIZ);
        strncat(new_request, user_agent_hdr, BUFSIZ);
        strncat(new_request, accept_line_hdr, BUFSIZ);
        strncat(new_request, connection_hdr, BUFSIZ);
        strncat(new_request, proxy_connection_hdr, BUFSIZ);
        strncat(new_request, end_line, BUFSIZ);
        // printf("new_request: \n\n%s\n", p);

        int request_length = 0;
        request_length = strlen(new_request);

        char request_to_forward[102400];
        int response_length = create_send_socket(sfd, port, host, new_request, request_length, request_to_forward);
        forward_bytes_to_client(connfd, request_to_forward, response_length);
    }
    else {
        // simply close connection and move on
    }

    Free(vargp);                    
    Close(connfd);
    return NULL;
}

int make_listening_socket(int port)
{
    memset(&ip4addr, 0, sizeof(struct sockaddr_in));
    ip4addr.sin_family = AF_INET;
    ip4addr.sin_port = htons(port);
    ip4addr.sin_addr.s_addr = INADDR_ANY;

    if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }
    if (bind(sfd, (struct sockaddr *)&ip4addr, sizeof(struct sockaddr_in)) < 0) {
        close(sfd);
        perror("bind error");
        exit(EXIT_FAILURE);
    }
    if (listen(sfd, 100) < 0) {
        close(sfd);
        perror("listen error");
        exit(EXIT_FAILURE);
    }
    return sfd;
}

void make_client(int port)
{
    int listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid; 

    listenfd = make_listening_socket(port);
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfdp = Malloc(sizeof(int)); 
        if ((*connfdp = accept(listenfd, (SA *) &clientaddr, &clientlen)) < 0){
            perror("Error with accepting connection!\n");
            break;
        }
        Pthread_create(&tid, NULL, thread, connfdp);
    }
}

int main(int argc, char *argv[])
{
    int port;
    if (argc < 2){
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    	exit(0);
    }
    signal(SIGPIPE, SIG_IGN);

    port = atoi(argv[1]);
    make_client(port);

    return 0;
}
