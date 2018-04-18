#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <string.h>
#include <netinet/in.h>
#include <unistd.h>

#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define BUF_SIZE 2048
#define MAXEVENTS 20

typedef struct {
    char* host;
    char* uri;
    char* headers;
} request_t;

/*  
    this is used to store the events with epoll
    states are:
    1) read client
    2) send server
    3) read server
    4) send client
*/
typedef struct {
    int serverfd;
    int clientfd;
    int bodysize;
    int bodybytes;
    int totalread;
    int headersize;
    int state;
    char* url;
    request_t* request;
    char* response;
    char* requestData;
}data;

CacheList cache;
FILE* logfile;
struct epoll_event event;
int efd;

int shouldQuit = 0;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

//handles SIGINT
void sigint_handler(int sig){
    shouldQuit = 1;
}

void initializeRequest(data* eventData){
    eventData->request = Malloc(sizeof(request_t));
    eventData->request->host = Malloc(MAXLINE);
    eventData->request->uri = Malloc(MAXLINE);
    eventData->request->headers = Malloc(MAX_OBJECT_SIZE);
}

//this creates a forward addres from the original request sent
char* createForwardRequest(request_t request){
    char* buf = (char*)Malloc(MAXLINE);

    sprintf(buf, "GET /%s HTTP/1.0\r\n", request.uri);
    strcat(buf, request.headers);

    return buf;
}

char* parseHeader(char* buf){
    char* line, *tag;
    line = (char*)Malloc(MAXLINE);
    tag = (char*)Malloc(MAXLINE);
    
    strcpy(line, buf);

    //get just the tag from the entire line
    strcpy(tag, strtok(buf, ":"));
    printf("%s\n", tag);

    //see if the tag matches any of the default tags.
    if(!strcmp(tag, "User-Agent"))
        strcpy(line, user_agent_hdr);
    if(!strcmp(tag, "Connection"))
        strcpy(line, connection_hdr);
    if(!strcmp(tag, "Proxy-Connection"))
        strcpy(line, proxy_connection_hdr);

    //put what is stored for the line in the original buf
    strcpy(buf, line);

    Free(line);
    Free(tag);

    return buf;
}

char* fileString(request_t request){
    char* reqStr = (char*)Malloc(MAX_CACHE_SIZE);
    strcpy(reqStr, "http://");
    strcat(reqStr, request.host);
    strcat(reqStr, "/");
    strcat(reqStr, request.uri);
    strcat(reqStr, "\n");

    return reqStr;
}

void logReq(request_t request, FILE* logfile){
    char* fileOutput = fileString(request);
    Fputs(fileOutput, logfile);
    Free(fileOutput);
}

//reads the initial request coming in from the client
void readClient(data* eventData, FILE* logfile){
    int confd = eventData->clientfd;
    char buf[MAXLINE];
    int requestsize;
    
    if(eventData->bodybytes == 0){
        eventData->requestData = Malloc(MAX_OBJECT_SIZE);
    }
    while((requestsize = read(confd, buf, MAX_OBJECT_SIZE)) != 0){
        if (requestsize < 0){
            if(errno == EWOULDBLOCK || errno == EAGAIN){
                if(strstr(eventData->requestData, "\r\n\r\n")){
                    printf("break client\n");
                    break;
                }
                return;
            }
        }
        else{
           memcpy(eventData->requestData + eventData->bodybytes, buf, requestsize);
           eventData->bodybytes += requestsize;
        }
    }
    initializeRequest(eventData);

    //get the host and uri
    int i = 0;
    while(eventData->requestData[i] != '/'){
        i++;
    }
    i++;
    i++;
    strcpy(eventData->request->host, "");
    while(eventData->requestData[i] != '/'){
        char temp[2];
        temp[0] = eventData->requestData[i];
        temp[1] = '\0';
        strcat(eventData->request->host, temp);
        i++;
    }
    i++;
    strcpy(eventData->request->uri, "");
    while(eventData->requestData[i] != ' '){
        char temp[2];
        temp[0] = eventData->requestData[i];
        temp[1] = '\0';
        strcat(eventData->request->uri, temp);
        i++;
    }
    i+=11;

    int count = 0;
    while(i < eventData->bodybytes){
        eventData->request->headers[count] = eventData->requestData[i];
        i++;
        count++;
    }
    eventData->request->headers[i] = '\0';

    // printf("%s/%s\n%s\n", eventData->request->host, eventData->request->uri, eventData->request->headers);

    logReq(*eventData->request, logfile);

    //check if the request is in the cache
    eventData->url = (char*)Malloc(MAXLINE);
    strcpy(eventData->url, eventData->request->host);
    strcat(eventData->url, "/");
    strcat(eventData->url, eventData->request->uri);

    // printf("url: %s\n", eventData->url);

    CachedItem* cacheEntry = find(eventData->url, &cache);
    if (cacheEntry != NULL){
        eventData->response = Malloc(MAX_OBJECT_SIZE);
        eventData->state = 4;
        // printf("%s %i\n", eventData->request->uri, eventData->state);
        // printf("%s\n", cacheEntry->item_p);

        memcpy(eventData->response, cacheEntry->item_p, cacheEntry->size);
        eventData->totalread = cacheEntry->size;
    }
    else{
        eventData->state = 2;
        printf("%s %i\n", eventData->request->uri, eventData->state);
        eventData->totalread = 0;
        eventData->bodybytes = 0;
    }
}

//forwards the data from initial request to server
void sendServer(data* eventData){
    // create the new request and send it
    char* serverhost = strtok(eventData->request->host, ":");
    char* serverport = strtok(NULL, " ");

    if(serverport == NULL)
        serverport = "80";
    
    //opens socket to the web server
    eventData->serverfd = Open_clientfd(serverhost, serverport);
    if (fcntl(eventData->serverfd, F_SETFL, fcntl(eventData->serverfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
        fprintf(stderr, "error setting socket option\n");
        exit(1);
    }


    event.data.ptr = eventData;
    event.events = EPOLLOUT | EPOLLET;

    //register the new fd with epoll
    if(epoll_ctl(efd, EPOLL_CTL_ADD, eventData->serverfd, &event) < 0){
        fprintf(stderr, "error adding event\n");
        exit(1);
    }

    //creates the request to send to the webserver
    char* forwardrequest = createForwardRequest(*eventData->request);

    //send to web server.
    Write(eventData->serverfd, forwardrequest, strlen(forwardrequest));
    Write(eventData->serverfd, "\r\n", 2);

    Free(forwardrequest);

    eventData->state = 3;
    printf("%s %i\n", eventData->request->uri, eventData->state);
}

//reads response from server
void readServer(data* eventData){
    
    int serverRead;

    char buf[MAX_OBJECT_SIZE];

    //get the response headers only if they haven't been read before
    if(eventData->totalread == 0){
        //change the event to be ready to write.
        event.events = EPOLLIN | EPOLLET;
        event.data.ptr = eventData;
        epoll_ctl(efd, EPOLL_CTL_MOD, eventData->serverfd, &event);
        eventData->response = Malloc(MAX_OBJECT_SIZE);
    }

    while((serverRead = read(eventData->serverfd, buf, MAX_OBJECT_SIZE)) != 0){
        if(serverRead < 0){
            if(errno == EWOULDBLOCK || errno == EAGAIN){
                return;
            }            
        }
        else{

            memcpy(eventData->response + eventData->totalread,
                buf, serverRead);

            eventData->totalread += serverRead;        
        }
    }

    //add to cache
    if(eventData->bodysize <= MAX_OBJECT_SIZE){
        //add response to the cache
        cache_URL(eventData->url, eventData->response, eventData->totalread, &cache);
    }

    //get rid of the event for serverfd
    close(eventData->serverfd);
    epoll_ctl(efd, EPOLL_CTL_DEL, eventData->serverfd, NULL);

    eventData->bodybytes = 0;
    eventData->state = 4; 
    // printf("%s %i\n", eventData->request->uri, eventData->state);     
}

//sends response to client
void sendClient(data* eventData){
    event.events = EPOLLOUT | EPOLLET;
    event.data.ptr = eventData;
    epoll_ctl(efd, EPOLL_CTL_MOD, eventData->clientfd, &event);

    // printf("%i\n", eventData->totalread);

    Write(eventData->clientfd, eventData->response, eventData->totalread);
    close(eventData->clientfd);

    //remove the finished epoll event
    epoll_ctl(efd, EPOLL_CTL_DEL, eventData->clientfd, NULL);

    eventData->state = -1;
}

//check get the state and forward to appropriate function
void handleEvent(data* eventData, FILE* logfile){
    if (eventData->state == 1){
        readClient(eventData, logfile);
    }
    if (eventData->state == 2){
        sendServer(eventData);
    }
    if (eventData->state == 3){
        readServer(eventData);
    }
    if (eventData->state == 4){
        sendClient(eventData);
    }
}

int main(int argc, char **argv)
{
    char* listenport;
	int listenfd, confd;
    struct sockaddr_storage clientaddr;
    socklen_t clientlen;
	struct epoll_event *events;
    int n;

    //makes sure there are enough arguements
    if(argc < 2){
        printf("usage: %s <port>\n", argv[0]);
        exit(1);
    }

    Signal(SIGINT, sigint_handler);

    //open epoll fd
    if((efd = epoll_create1(0)) < 0){
        fprintf(stderr, "Error creating epoll");
        exit(1);
    }

    //open listenfd
    listenport = argv[1];
    listenfd = Open_listenfd(listenport);

    //set to non blocking
    if (fcntl(listenfd, F_SETFL, fcntl(listenfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
		fprintf(stderr, "error setting socket option\n");
		exit(1);
	}

    //register the listen fd with epoll
    event.data.fd = listenfd;
	event.events = EPOLLIN | EPOLLET; // use edge-triggered monitoring
	if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &event) < 0) {
		fprintf(stderr, "error adding event\n");
		exit(1);
	}

    events = calloc(MAXEVENTS, sizeof(event));

    //open the logging file
    logfile = Fopen("log.txt", "w");

    cache_init(&cache);

    //for each incoming request
    for(;;){       
        n = epoll_wait(efd, events, MAXEVENTS, 1000);

        //check if handler has been called on timeout
        if(n == 0)
            if(shouldQuit)
                break;
            else
                continue;

        //on error from epoll wait
        else if(n < 0){
            fprintf(stderr, "epoll_wait failure");
            break;
        }

        else{
            for(int i = 0; i < n; i++){
                //error on fd
                if ((events[i].events & EPOLLERR) ||
					(events[i].events & EPOLLHUP) ||
					(events[i].events & EPOLLRDHUP)) {
                    fprintf (stderr, "epoll error on fd %d\n", events[i].data.fd);
                    close(events[i].data.fd);
                    continue;
			    }

                //if there is a connection(s) ready on the listenfd
                if (listenfd == events[i].data.fd) {
                    clientlen = sizeof(struct sockaddr_storage); 

                    //get all available connections
                    while ((confd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen)) > 0) {

                        //set confd to non-blocking
                        if (fcntl(confd, F_SETFL, fcntl(confd, F_GETFL, 0) | O_NONBLOCK) < 0) {
                            fprintf(stderr, "error setting socket option\n");
                            exit(1);
                        }

                        //create the client data struct
                        data* clientData = Malloc(MAX_OBJECT_SIZE);
                        clientData->serverfd = -2;
                        clientData->clientfd = confd;
                        clientData->bodysize = -2;
                        clientData->bodybytes = 0;
                        clientData->state = 1;
                        clientData->totalread = 0;

                        // add event to epoll file descriptor
                        event.data.ptr = clientData;
                        event.events = EPOLLIN | EPOLLET; // use edge-triggered monitoring
                        if (epoll_ctl(efd, EPOLL_CTL_ADD, confd, &event) < 0) {
                            fprintf(stderr, "error adding event\n");
                            exit(1);
                        }
                    }

                    if (errno == EWOULDBLOCK || errno == EAGAIN) {
                        // no more clients to accept()
                    } else {
                        perror("error accepting");
                    }
			    }

                //handle the ready fds
                else{
                    handleEvent((data*)events[i].data.ptr, logfile);
                }
            }
        }
    }

    cache_destruct(&cache);
    close(efd);
    Free(events);
    Fclose(logfile);
    if(shouldQuit)
        exit(0);

    exit(1);
}