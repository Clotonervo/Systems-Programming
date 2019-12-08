#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include "csapp.h"
#include<sys/epoll.h>
#include<errno.h>
#include<fcntl.h>



/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NTHREADS 30
#define SBUFSIZE 500
#define LOGSIZE 500
#define MAXEVENTS 64
#define READ_FROM_CLIENT 1
#define SEND_TO_SERVER 2
#define READ_FROM_SERVER 3
#define SEND_TO_CLIENT 4


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

void print_to_log_file(char* url);
FILE *logfile;


int parse_request(char* request, char* type, char* protocol, char* host, char* port, char* resource, char* version);
void parse_host_and_port(char* request, char* host, char* port);
void make_url(char* url, char* protocal, char* port, char* host, char* resource);

int efd;


struct event_list_node {
    int client_fd;
    int server_fd;
    int read_from_client;
    int written_to_server;
    int read_from_server;
    int written_to_client;
    int state;
    char* request;
    char* response;
    char* url;
    struct event_list_node* next;
};

struct event_list_head {
    struct event_list_node* next;
};

struct event_list_head* info_head;


void read_from_client(struct event_list_node* current);
void send_to_server(struct event_list_node* current);
void read_from_server(struct event_list_node* current);
void send_to_client(struct event_list_node* current);
void remove_from_list(struct event_list_node* current);

/* ------------------------------------- main() -------------------------------------------------*/
int main(int argc, char *argv[])
{
    // printf("start program");
    int port;
    if (argc < 2){
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    	exit(0);
    }

    port = atoi(argv[1]);


    int listenfd, connfd;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	struct epoll_event event;
	struct epoll_event *events;
	int i;
	int len;

	size_t n; 
	char buf[MAXLINE]; 

    logfile = fopen("proxy_log.txt", "w");
    fprintf(logfile, "<---------------------- LOG FILE OPEN ---------------------------->\n");

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}

	listenfd = Open_listenfd(argv[1]);

	// set fd to non-blocking (set flags while keeping existing flags)
	if (fcntl(listenfd, F_SETFL, fcntl(listenfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
		fprintf(stderr, "error setting socket option\n");
		exit(1);
	}

	if ((efd = epoll_create1(0)) < 0) {
		fprintf(stderr, "error creating epoll fd\n");
		exit(1);
	}

	event.data.fd = listenfd;
	event.events = EPOLLIN | EPOLLET; 
	if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &event) < 0) {
		fprintf(stderr, "error adding event\n");
		exit(1);
	}

    info_head = calloc(4, sizeof(struct event_list_head));
	events = calloc(MAXEVENTS, sizeof(event));

	while (1) {
		n = epoll_wait(efd, events, MAXEVENTS, -1);

		for (i = 0; i < n; i++) {
			if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (events[i].events & EPOLLRDHUP)) {
				fprintf (stderr, "epoll error on fd %d\n", events[i].data.fd);
				close(events[i].data.fd);
				continue;
			}

			if (listenfd == events[i].data.fd) { 
                fprintf(logfile, "Establishing new event\n");
				clientlen = sizeof(struct sockaddr_storage); 

				while ((connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen)) > 0) {
					if (fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
						fprintf(stderr, "error setting socket option\n");
						exit(1);
					}

					event.data.fd = connfd;
					event.events = EPOLLIN | EPOLLET; 
					if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &event) < 0) {
						fprintf(stderr, "error adding event\n");
						exit(1);
					}

                    struct event_list_node* current = info_head->next;

                    struct event_list_node* new_node;
                    new_node = calloc(2, sizeof(struct event_list_node));
                    new_node->state = READ_FROM_CLIENT;
                    new_node->client_fd = connfd;
                    new_node->server_fd = -1;
                    new_node->read_from_client = 0;
                    new_node->read_from_server = 0;
                    new_node->written_to_server = 0;
                    new_node->written_to_client = 0;
                    new_node->response = calloc(2, MAX_OBJECT_SIZE);
                    new_node->request = calloc(2, MAX_OBJECT_SIZE);
                    new_node->url = calloc(1, MAXBUF);
                    
                    if(info_head->next == NULL){
                        info_head->next = new_node;
                    }
                    else {
                        while(current->next){
                            current = current->next;
                        }
                        current->next = new_node;
                    }

				}

				if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    continue;
				} 
                else {
					perror("error accepting");
				}
			} 
            else { 
                fprintf(logfile, "Event triggered on: %d\n", events[i].data.fd);
                struct event_list_node* current = info_head->next;
                fflush(logfile);

                while(current){
                    if(events[i].data.fd == current->client_fd || events[i].data.fd == current->server_fd){
                        if(current->state == READ_FROM_CLIENT){
                            fprintf(logfile, "Going into read_from_client function\n");
                            fflush(logfile);
                            read_from_client(current);
                        }
                        else if (current->state == SEND_TO_SERVER){
                            fprintf(logfile, "Going into send_to_server function\n");
                            fflush(logfile);
                            send_to_server(current);
                        }
                        else if (current->state == READ_FROM_SERVER){
                            fprintf(logfile, "Going into read_from_server function\n");
                            fflush(logfile);
                            read_from_server(current);
                        }
                        else if (current->state == SEND_TO_CLIENT){
                            fprintf(logfile, "Going into send_to_client function\n");
                            fflush(logfile);
                            send_to_client(current);
                        }
                        break;
                    }
                    else {
                        current = current->next;
                    }

                }
			}
		}
	}
	free(events);
    return 0;
}

/* ----------------------------------------------------------------------------------------------------------------------------------------*/
/* ----------------------------------------------------- READ FROM CLIENT -----------------------------------------------------------------*/
/* ----------------------------------------------------------------------------------------------------------------------------------------*/
void read_from_client(struct event_list_node* current)
{
    char* p = current->request;
    int nread = 0;
    while(1){
        nread = recv(current->client_fd, (p + current->read_from_client), MAXBUF, 0);

        char temp_buf[MAXBUF] = {0};
        strcpy(temp_buf, p + current->read_from_client + nread - 4);

        if (!strcmp(temp_buf, "\r\n\r\n")){
            current->read_from_client += nread;
            break;
        }

        if(nread > 0){
            current->read_from_client += nread;
        }
        else if (nread < 0){
            return;
        }
    }

    fprintf(logfile, "Read all request bytes from client!\n");
    // fprintf(logfile, "request = %s \n", ->request);


    char request[MAXBUF] = {0};
    char type[BUFSIZ] = {0};
    char host[BUFSIZ];
    char port[BUFSIZ];
    char resource[BUFSIZ] = {0};
    char protocal[BUFSIZ] = {0};
    char version[BUFSIZ] = {0};
    char url[BUFSIZ] = {0};
    int sfd = 0;
    int req_val = 0;

    req_val = parse_request(current->request, type, protocal, host, port, resource, version);
    make_url(url, protocal, port, host, resource);
    print_to_log_file(url);
    strcpy(current->url, url);
    fprintf(logfile, "After url is copied over \n");

    if (req_val == 0){
        int response_length = 0;
        char request_to_forward[MAX_OBJECT_SIZE];        
        char new_request[MAXBUF] = {0};

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
        
        int request_length = 0;
        current->read_from_client = strlen(new_request);
        fprintf(logfile, "before we copy the new request over \n");
        fflush(logfile);
        memcpy(current->request, new_request, current->read_from_client);

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

        if (fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
		    fprintf(stderr, "error setting socket option\n");
		    exit(1);
	    }

        fprintf(logfile, "Created Socket, now adding to events\n");

        struct epoll_event new_event;
        new_event.events = EPOLLOUT | EPOLLET;
        new_event.data.fd = sfd;
        current->server_fd = sfd;
        current->state = SEND_TO_SERVER;

        if (epoll_ctl(efd, EPOLL_CTL_ADD, current->server_fd, &new_event) < 0) {
            fprintf(stderr, "error adding event\n");
            exit(1);
        }        

    }
    else {
        // simply close connection and move on
    }

    fprintf(logfile, "Exiting read_client_request function\n");
    fflush(logfile);
    return;
}

/* ----------------------------------------------------------------------------------------------------------------------------------------*/
/* ----------------------------------------------------- SEND TO SERVER -----------------------------------------------------------------*/
/* ----------------------------------------------------------------------------------------------------------------------------------------*/
void send_to_server(struct event_list_node* current)
{
    int nread = 0;
    char* p = current->request;

    while(current->read_from_client > 0){

        nread = send(current->server_fd, (p + current->written_to_server), MAXBUF, 0);

        if (nread > 0){
            current->written_to_server += nread;
            current->read_from_client -= nread;
        }
        else if (nread == 0){
            break;
        }
        else if (nread < 0){
            return;
        }
    }

    struct epoll_event new_event;
    new_event.events = EPOLLIN | EPOLLET;
    new_event.data.fd = current->server_fd;
    current->state = READ_FROM_SERVER;

    if (epoll_ctl(efd, EPOLL_CTL_MOD, current->server_fd, &new_event) < 0) {
        fprintf(stderr, "error changing event\n");
        exit(1);
    }   

    return;
}


/* ----------------------------------------------------------------------------------------------------------------------------------------*/
/* ----------------------------------------------------- READ FROM SERVER -----------------------------------------------------------------*/
/* ----------------------------------------------------------------------------------------------------------------------------------------*/
void read_from_server(struct event_list_node* current)
{
    int nread = 0;
    char* p = current->response;
    while(1){
        nread = recv(current->server_fd, (p + current->read_from_server), MAXBUF, 0);
        if(nread > 0){
            current->read_from_server += nread;
        }
        else if (nread == 0){
            break;
        }
        else if (nread < 0){
            return;
        }
    }

    if (epoll_ctl(efd, EPOLL_CTL_DEL, current->server_fd, NULL) < 0) {
        fprintf(stderr, "error deleting serverfd event\n");
        exit(1);
    } 
    close(current->server_fd);

    struct epoll_event new_event;
    new_event.events = EPOLLOUT | EPOLLET;
    new_event.data.fd = current->client_fd;
    current->state = SEND_TO_CLIENT;

    if (epoll_ctl(efd, EPOLL_CTL_MOD, current->client_fd, &new_event) < 0) {
        fprintf(stderr, "error changing event\n");
        exit(1);
    }  

    return;
}

/* ----------------------------------------------------------------------------------------------------------------------------------------*/
/* ----------------------------------------------------- SEND TO CLIENT -----------------------------------------------------------------*/
/* ----------------------------------------------------------------------------------------------------------------------------------------*/
void send_to_client(struct event_list_node* current)
{
    int nread = 0;
    char* p = current->response;
    while(current->read_from_server > 0){
        nread = send(current->client_fd, (p + current->written_to_client), current->read_from_server, 0);

        if (nread > 0){
            current->written_to_client += nread;
            current->read_from_server -= nread;
        }
        else if (nread == 0){
            break;
        }
        else if (nread < 0){
            return;
        }
    }

    if (epoll_ctl(efd, EPOLL_CTL_DEL, current->client_fd, NULL) < 0) {
        fprintf(stderr, "error deleting serverfd event\n");
        exit(1);
    } 
    close(current->client_fd);
    current->state = 0;
    current->client_fd = -1;
    current->server_fd = -1;
    //delete from list
    return;
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


void make_url(char* url, char* protocal, char* port, char* host, char* resource)
{
    char final_url[MAXLINE] = {0};
    char* p = final_url;

    strcpy(final_url, protocal);
    strcat(final_url, "://");
    strcat(final_url, host);
    if(strcmp(port, "80")){
        strcat(final_url, colon);
        strcat(final_url, port);
    }
    strcat(final_url, resource);
    strcat(final_url, "\n\0");

    strcpy(url, p);
}

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

void print_to_log_file(char* url)
{
    char message[MAXBUF] = {0};
    char* p = message;
    time_t now;
    char time_str[MAXLINE] = {0};

    now = time(NULL);
    strftime(time_str, MAXLINE, "%d %b %H:%M:%S ", localtime(&now));
    strcpy(message, time_str);
    strcat(message, ">|  ");
    strcat(message, url);

    fprintf(logfile, "%s", p);
    fflush(logfile);
}

