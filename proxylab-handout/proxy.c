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
static const char *connection_hdr = "Connection: close";
static const char *proxy_connection_hdr = "Proxy-Connection: close";
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
        strcpy(host, temp);

        char* p = strchr(host,':');
        char* n = strchr(host, '\n');
        if (p < n){
            *p = '\0';
            char* temp_p = p + 1;
            p = strchr(temp_p,'\n');
            *p = '\0';
            strcpy(port, temp_p);
        }
        else {
            strcpy(port, "80");
            *n = '\0';
        }
    }
    else {
        return;
    }
}

int parse_request(char* request, char* type, char* protocol, char* host, char* port, char* resource, char* version){
	char url[MAXBUF];
	
	if((!strstr(request, "/")) || !strlen(request))
		return -1;

    parse_host_and_port(request, host, port);
	
	strcpy(resource, "/");
	sscanf(request,"%s %s %s", type, url, version);
	
	if (strstr(url, "://")) 
		sscanf(url, "%[^:]://%*[^/]%s", protocol, resource);
	else
		sscanf(url, "[^/]%s", resource);
		
	return 0;
}

char* read_bytes(int fd, char* p)
{
    // printf("in read_bytes");
    sleep(1);
    int total_read = 0;
    ssize_t nread = 0;
    for (;;) {
		nread = recv(fd, (p + total_read), MAXBUF, 0);
        total_read += nread;
        // printf(" %d ", nread);

		if (nread == -1)
			continue;              

        if (nread == 0)
            break;

	}
    // printf("Received:%s", p);
}

void *thread(void *vargp) 
{  
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self()); 
    char request[MAXBUF];
    char type[MAXBUF];
    char host[MAXBUF];
    char port[MAXBUF];
    char resource[MAXBUF];
    char protocal[MAXBUF];
    char version[MAXBUF];

    int req_val = 0;
    printf("in thread function\n");
    read_bytes(connfd,request);
    // printf("%s\n", request);

    req_val = parse_request(request, type, protocal, host, port, resource, version);
    printf("request = %s\n", request);
    printf("type = %s\n", type);
    printf("protocal = %s\n", protocal);
    printf("host = %s\n", host);
    printf("port = %s\n", port);
    printf("resource = %s\n", resource);
    printf("version = %s\n", version);

    if (req_val == 0){

    }
    else {
        // simply close connection and move on
    }

    Free(vargp);                    
    // echo(connfd);
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
        *connfdp = Accept(listenfd, (SA *) &clientaddr, &clientlen); 
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
