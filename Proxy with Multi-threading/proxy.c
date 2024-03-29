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


/* HTTP Request Helpers */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static const char *host_init = "Host: ";
static const char *accept_line_hdr = "Accept */* \r\n";
static const char *end_line = "\r\n";
static const char *default_port = "80";
static const char *version_hdr = " HTTP/1.0";
static const char *colon = ":";

/*---------------------- Structs and other global variables ------------------*/
FILE *logfile;

//---------- Cache
typedef struct cache_node{
    char* url;
    char* response;
    int size;
    struct cache_node* next;
    struct cache_node* previous;
} cache_node;

typedef struct {
    int cache_size;
    int number_of_objects;
    struct cache_node* head;
} cache_list;

cache_list head_cache;

//------------- Sockets and File descriptors
int efd;
int listenfd;
struct sockaddr_in ip4addr;
struct epoll_event *events;

//---------------- Events data structure
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

/*----------------------------- Functions -----------------------------*/
void print_to_log_file(char* url);

//---------- Helpter Functions
int parse_request(char* request, char* type, char* protocol, char* host, char* port, char* resource, char* version);
void parse_host_and_port(char* request, char* host, char* port);
void make_url(char* url, char* protocal, char* port, char* host, char* resource);
int make_listening_socket(int port);
void sigint_handler(int sig);

//----------- Epoll states
void read_from_client(struct event_list_node* current);
void send_to_server(struct event_list_node* current);
void read_from_server(struct event_list_node* current);
void send_to_client(struct event_list_node* current);
void remove_from_list(struct event_list_node* current);

//---------- Cache
void free_cache(cache_list *cache);
int get_data_from_cache(cache_list *cache, char* url, char* response);
int cache_search(cache_list *cache, char* url);
void add_cache(cache_list *cache, char* url, char* content, int size);
void cache_init(cache_list *cache);


/* ------------------------------------- main() -------------------------------------------------*/
int main(int argc, char *argv[])
{
    int port;
    if (argc < 2){
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    	exit(0);
    }

    port = atoi(argv[1]);
    signal(SIGINT, sigint_handler);


    int connfd;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	struct epoll_event event;
	int i;
	size_t n; 

    logfile = fopen("proxy_log.txt", "w");
    fprintf(logfile, "<---------------------- LOG FILE OPEN ---------------------------->\n");
    cache_init(&head_cache);

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}

	listenfd = make_listening_socket(port);

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
                struct event_list_node* current = info_head->next;

                while(current){
                    if(events[i].data.fd == current->client_fd || events[i].data.fd == current->server_fd){
                        if(current->state == READ_FROM_CLIENT){
                            read_from_client(current);
                        }
                        else if (current->state == SEND_TO_SERVER){
                            send_to_server(current);
                        }
                        else if (current->state == READ_FROM_SERVER){
                            read_from_server(current);
                        }
                        else if (current->state == SEND_TO_CLIENT){
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

    if (req_val == 0){
        char new_request[MAXBUF] = {0};

        if(cache_search(&head_cache, url)){
            current->read_from_server = get_data_from_cache(&head_cache, url, current->response);
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
        
        current->read_from_client = strlen(new_request);
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

    if(!cache_search(&head_cache, current->url)){
        add_cache(&head_cache, current->url, current->response, current->written_to_server);
    }

    if (epoll_ctl(efd, EPOLL_CTL_DEL, current->server_fd, NULL) < 0) {
        fprintf(stderr, "error deleting serverfd event\n");
        exit(1);
    } 
    close(current->server_fd);

    if (epoll_ctl(efd, EPOLL_CTL_DEL, current->client_fd, NULL) < 0) {
        fprintf(stderr, "error deleting serverfd event\n");
        exit(1);
    } 
    close(current->client_fd);

    remove_from_list(current);

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

int make_listening_socket(int port)
{
    memset(&ip4addr, 0, sizeof(struct sockaddr_in));
    ip4addr.sin_family = AF_INET;
    ip4addr.sin_port = htons(port);
    ip4addr.sin_addr.s_addr = INADDR_ANY;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }
    if (bind(listenfd, (struct sockaddr *)&ip4addr, sizeof(struct sockaddr_in)) < 0) {
        close(listenfd);
        perror("bind error");
        exit(EXIT_FAILURE);
    }
    if (listen(listenfd, 100) < 0) {
        close(listenfd);
        perror("listen error");
        exit(EXIT_FAILURE);
    }
    return listenfd;
}

/* ------------------------------------- Deregistering sockets/removing from data structure -------------------------------------------------*/
void remove_from_list(struct event_list_node* node_to_remove)
{
    if(info_head->next == node_to_remove){
        free(node_to_remove->url);
        free(node_to_remove->response);
        free(node_to_remove->request);
        info_head->next = node_to_remove->next;
        free(node_to_remove);
    }
    else{
        struct event_list_node* parent = info_head->next;
        while(parent->next != node_to_remove){
            parent = parent->next;
        }
        free(node_to_remove->url);
        free(node_to_remove->response);
        free(node_to_remove->request);
        parent->next = node_to_remove->next;
        free(node_to_remove);
    }
    return;
}

/* ------------------------------------- SIGINT handler -------------------------------------------------*/

void sigint_handler(int sig)  
{
    fprintf(logfile, "In sigint handler\n");
    free_cache(&head_cache);
    free(info_head);
    fclose(logfile);
    free(events);

    exit(0);
}

/* ------------------------------------- Logger -------------------------------------------------*/
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

    return;
}

/* ------------------------------------------- Cache ---------------------------------------*/
void cache_init(cache_list *cache) 
{
    cache->cache_size = 0;
    cache->number_of_objects = 0;
    cache->head = NULL;
}

void add_cache(cache_list *cache, char* url, char* content, int size)
{
    if (size > MAX_OBJECT_SIZE){
        return;
    }
    
    if (cache->cache_size + size > MAX_CACHE_SIZE){
        return;
    }

    cache->cache_size += size;
    cache->number_of_objects += 1;

    cache_node* new_node = malloc(sizeof(cache_node));
    new_node->next = cache->head;
    new_node->url = malloc(strlen(url) * sizeof(char));
    new_node->response = malloc(size);
    new_node->size = size;
    strcpy(new_node->url, url);
    memcpy(new_node->response, content, size);

    cache->head = new_node;

}

int cache_search(cache_list *cache, char* url)
{       
    struct cache_node *current = cache->head;
    int result = 0;

    while(current){
        if (!strcmp(current->url, url)) { 
            result = 1;
        }
        current = current->next;
    }
    return result;
}

int get_data_from_cache(cache_list *cache, char* url, char* response)
{
    // printf("Getting data from cache\n");
    struct cache_node *current = cache->head;
    int result = 0;

    while(current){

        if (!strcmp(current->url, url)) { 
            memcpy(response, current->response, current->size);
            result = current->size;
            break;
        }
        current = current->next;
    }

    return result;
}

void free_cache(cache_list *cache)
{
    struct cache_node *current = cache->head;
    while(current){
        free(current->url);
        free(current->response);
        struct cache_node *temp = current->next;
        free(current);
        current = temp;
    }
}



