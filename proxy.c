#include "csapp.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define MAXEVENTS 64
#define BUFMAX 102400

void command(void);
//int handle_client(int *clientfd, int *serverfd, size_t *totalBytesPassed, size_t *totalDataSize, unsigned char *dataBuf, char *URL, serviceState *state);

typedef struct event_action
{
    bool (*callback)(struct event_action * /*int, int, size_t, size_t, unsigned char *, char *, serviceState **/);
    int clientfd;
    int serverfd;
    size_t totalBytesPassed;
    size_t totalDataSize;
    char *dataBuf;
    char *URL;
    //serviceState *state;
} event_action;

bool runProxy;

//One way of thinking about the problem is in terms of "states":
//CLREAD: reading from client, until the entire request has been read from the client.
//SRVWRITE: writing to server, until the entire request has been sent to the server.
//SRVREAD: reading from server, until the entire response has been received.
//CLWRITE: writing to client, until the entire response has been sent to the client.
//typedef enum {CLREAD, SRVWRITE, SRVREAD, CLWRITE} serviceState;

//These instructions will be returned by
//typedef enum {NOCHANGE, GOTOSRVWRITE, GOTOSRVREAD, GOTOCLWRITE, ENDSERVICE} transitionInstruction;

//TODO: Make a different event function for each state
bool handle_new_client(event_action *eventAction);
bool clread(event_action *eventAction /*int *clientfd, int *serverfd, size_t *totalBytesPassed, size_t *totalDataSize, unsigned char *dataBuf, char *URL, serviceState *state*/);
bool srvwrite(event_action *eventAction /*int *clientfd, int *serverfd, size_t *totalBytesPassed, size_t *totalDataSize, unsigned char *dataBuf, char *URL, serviceState *state*/);
bool srvread(event_action *eventAction /*int *clientfd, int *serverfd, size_t *totalBytesPassed, size_t *totalDataSize, unsigned char *dataBuf, char *URL, serviceState *state*/);
bool clwrite(event_action *eventAction /*int *clientfd, int *serverfd, size_t *totalBytesPassed, size_t *totalDataSize, unsigned char *dataBuf, char *URL, serviceState *state*/);

void freeEventAction(event_action *);

int efd;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connectionHeader = "\r\nConnection: close";
static const char *proxyHeader = "\r\nProxy-Connection: close";

static int currentCacheSize;
static const int wirelen = 2000;

typedef struct
{
    int objectSize;
    unsigned long timeLastAccessed;
    char request[1024];
    char *data; //[MAX_OBJECT_SIZE];
} cachentry;

cachentry *cache[MAX_CACHE_SIZE];

FILE *Log;

int main(int argc, char **argv)
{
    struct epoll_event event;
    struct epoll_event *events;
    int i;
    
    event_action *eventAction;

    size_t n;
    
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(EXIT_SUCCESS);
    }

    //Create an epoll instance with epoll_create1()
    if ((efd = epoll_create1(0)) < 0)
    {
        fprintf(stderr, "error creating epoll fd\n");
        exit(EXIT_FAILURE);
    }

    //Set up listen socket.
    int listenfd = Open_listenfd(argv[1]);

    // set fd to non-blocking (set flags while keeping existing flags)
    if (fcntl(listenfd, F_SETFL, fcntl(listenfd, F_GETFL, 0) | O_NONBLOCK) < 0)
    {
        fprintf(stderr, "error setting socket option\n");
        exit(EXIT_FAILURE);
    }

    //Set up event action to associate with listenfd
    eventAction = malloc(sizeof(event_action));
    eventAction->callback = handle_new_client;//this may need & in front of it?
    
    int clientfd = listenfd;
    //For the listening file descriptor the clientfd will be used to accept connections.
    eventAction->clientfd = clientfd;
    eventAction->serverfd = -1;
    eventAction->totalBytesPassed = 0;
    eventAction->totalDataSize = 0;
    eventAction->URL = NULL;
    //eventAction->serviceState = NULL;

    event.data.ptr = eventAction;
    event.events = EPOLLIN | EPOLLET; // Read mode, use edge-triggered monitoring

    //Register your listen socket with the epoll instance.
    if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &event) < 0)
    {
        fprintf(stderr, "error adding event\n");
        exit(EXIT_FAILURE);
    }

    //Buffer where events are returned
    events = calloc(MAXEVENTS, sizeof(event));

    //Open your Log file.
    Log = fopen("proxyLog.txt", "a");
    if (!Log)
    {
        fprintf(stderr, "Log file failed to initialize!\n");
        exit(EXIT_FAILURE);
    }

    fprintf(Log, "--- Begin Proxy Session at UNIX Time %ld ---", time(NULL));

    //Initialize cache.
    for (int i = 0; i < MAX_CACHE_SIZE; i++)
    {
        cache[i] = NULL;
    }

    currentCacheSize = 0;

    //This flag can be used to gracefully end the main loop and free resources before shutdown
    runProxy = true;

    //Start an epoll_wait() loop.
    while (runProxy)
    {
        // wait for event to happen with timeout of 1 second
        n = epoll_wait(efd, events, MAXEVENTS, 1);

        //If the result was a timeout (i.e., return value from epoll_wait() is 0), check if a global flag has been set by a handler and, if so, end the loop; otherwise, continue waiting
        if (!n)
        {
            /*if ()
            {
            }*/
        }
        else if (n < 0) //If the result was an error, handle the error appropriately.
        {
            switch
                errno
                {
                case EBADF:
                    fprintf(stderr, "efd is not a valid file descriptor\n");
                    runProxy = false;
                    break;
                case EFAULT:
                    fprintf(stderr, "Event pointer doesn't have write permissions\n");
                    runProxy = false;
                    break;
                case EINVAL:
                    fprintf(stderr, "efd is not registered with epoll or maxevents <= 0\n");
                    runProxy = false;
                    break;
                }
        }
        else //If there was no error, you should loop through all the events and handle each appropriately.
        {
            for (i = 0; i < n; i++)
            {
                eventAction = (struct event_action *)events[i].data.ptr;
                //argptr = eventAction->arg;
                if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
                {
                    /* An error has occured on this fd */
                    fprintf(stderr, "epoll error on event associated with client fd %d\n", eventAction->clientfd);
                    freeEventAction(eventAction);
                }

                //rewrite this to handle state transitions
                else if (eventAction->callback(eventAction /*&eventAction->clientfd, &eventAction->serverfd, &eventAction->totalBytesPassed, &eventAction->totalDataSize, &eventAction->dataBuf, &eventAction->URL, &eventAction->state*/))
                {
                    //free event action struct memory
                    freeEventAction(eventAction);
                }
            }
        }
    }

    printf("Shutting down...\n");

    //After your epoll_wait() loop, you should clean up any resources (e.g., freeing malloc'd memory), and exit.
    //iterate through events and free?
    free(events);

    //close Log.
    fclose(Log);

    //free cache data
    for (int i = 0; i < MAX_CACHE_SIZE; i++)
    {
        if (cache[i])
        {
            if (cache[i]->data)
            {
                free(cache[i]->data);
            }
            free(cache[i]);
        }
    }

    return EXIT_SUCCESS;
}

//If an event corresponds to the listen socket, you should accept() any and all client connections
bool handle_new_client(event_action *eventAction /*int listenfd*/)
{
    //TODO: rewrite to use event action
    socklen_t clientlen;
    int connfd;
    struct sockaddr_storage clientaddr;
    struct epoll_event event;
    
    event_action *eventActionForClient;

    clientlen = sizeof(struct sockaddr_storage);

    // loop and get all the connections that are available
    while ((connfd = accept(eventAction->clientfd, (struct sockaddr *)&clientaddr, &clientlen)) > 0)
    {

        // set fd to non-blocking (set flags while keeping existing flags)
        if (fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK) < 0)
        {
            fprintf(stderr, "error setting socket option\n");
            //exit(1);
            return false;
        }

        eventActionForClient = malloc(sizeof(event_action));
        //TODO: change this to state 1 handler after state 1 handler is created.
        eventActionForClient->callback = clread;
        
        int clientfd = connfd;
        eventActionForClient->clientfd = clientfd;
        eventActionForClient->serverfd = -1;
        eventActionForClient->totalBytesPassed = 0;
        eventActionForClient->totalDataSize = 0;
        eventActionForClient->dataBuf = NULL;
        eventActionForClient->URL = NULL;
        //eventAction->serviceState = CLREAD;

        event.data.ptr = eventActionForClient;
        // add event to epoll file descriptor
        event.events = EPOLLIN | EPOLLET; //Read mode, use edge-triggered monitoring
        if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &event) < 0)
        {
            fprintf(stderr, "error adding event\n");
            freeEventAction(eventActionForClient);
            //exit(1);
            return false;
        }
    }

    if (errno == EWOULDBLOCK || errno == EAGAIN)
    {
        // no more clients to accept()
        return false;
    }
    else
    {
        perror("error accepting");
        return false;
    }
}

void freeEventAction(event_action *eventAction)
{
    close(eventAction->clientfd);
    //free's may need & in front of them...
    //free(eventAction->clientfd >= 0);

    if (eventAction->serverfd)
    {
        close(eventAction->serverfd);
    }
    if (eventAction->dataBuf)
    {
        free(eventAction->dataBuf);
    }
    if (eventAction->URL)
    {
        free(eventAction->URL);
    }

    free(eventAction);
    eventAction = NULL;
}

//If an event corresponds to the socket associated with a client request (client socket) or the socket associated with the proxied request to an upstream server (server socket) , you should determine where you are in terms of handling the corresponding client request and begin (or resume) handling it.  You should only read() or write() on said socket if your event indicates that you can, and only until the read() or write() call returns a value less than 0.  In such cases (where a value less than 0 is returned), if errno is EAGAIN or EWOULDBLOCK, then that is an indicator that there is no more data to be read, or (for write) that the file descriptor is no longer available for writing.  See the "Client Request Handling" section for more information.

int handle_client(int connfd)
{
    int len;
    char buf[MAXLINE];
    while ((len = recv(connfd, buf, MAXLINE, 0)) > 0)
    {
        printf("Received %d bytes\n", len);
        send(connfd, buf, len, 0);
    }
    if (len == 0)
    {
        // EOF received.
        // Closing the fd will automatically unregister the fd
        // from the efd
        return 0;
    }
    else if (errno == EWOULDBLOCK || errno == EAGAIN)
    {
        return 1;
        // no more data to read()
    }
    else
    {
        perror("error reading");
        return 0;
    }
}

//For example, you'll need to associate with the request: the file descriptors corresponding to the client socket and the server socket; the request state (see Client Request States); the total number of bytes to read or write for the request or the response; the total number of bytes read or written thus far; the buffer to which you are writing data from a socket or from which you are writing the data to a socket; etc.

//When you have completed all the I/O (reading or writing) for a given state, there are often items not related to socket I/O that you can perform before you move to the next state.  For example, in the READ_REQUEST state, when you have finished reading the entire request, you can immediately Log the request, check the cache, and (if not in the cache), set up to the server socket (including making it non-blocking) and call connect().  After all that, then you can transition to the SEND_REQUEST state (or to the SEND_RESPONSE state, in the case the item was cached).  However, as indicated previously, you may not write until the socket becomes ready for writing--that is, until it is returned by epoll_wait() as being write ready.

//CLREAD: reading from client, until the entire request has been read from the client.
bool clread(event_action *eventAction /*int *clientfd, int *serverfd, size_t *totalBytesPassed, size_t *totalDataSize, unsigned char *dataBuf, char *URL, serviceState *state*/)
{
    bool endOfReading = false;
    int clientfd = eventAction->clientfd;
    size_t len = 0;
    char buf[BUFMAX];
    char tempBuf[BUFMAX];
    memset(tempBuf, '\0', BUFMAX);

    if (!eventAction->URL)
    {
        eventAction->URL = calloc(BUFMAX, sizeof(char));
    }

    while ((len = recv(clientfd, tempBuf, BUFMAX - eventAction->totalBytesPassed, 0)) > 0)
    {
        strcat(buf, tempBuf);
        eventAction->totalBytesPassed += len;
        //printf("Received %d bytes\n", len);
        //send(connfd, buf, len, 0);
    }
    if (len == 0)
    {
        // EOF received.
        endOfReading = true;
    }
    /*else if (errno == EWOULDBLOCK || errno == EAGAIN)
    {
        // no more data to read() for now
        return false;
    }
    else
    {
        perror("error reading");
        return true;
    }*/
    //When a return value to read() or write() is less than 0 and errno is EAGAIN or EWOULDBLOCK, it is an indicator that you are done for the moment--but you need to know where you should start next time it's your turn
    else if (errno != EWOULDBLOCK && errno != EAGAIN)
    {
        perror("error reading");
        return true;
    }

    strcat(eventAction->URL, tempBuf);

    if (endOfReading)
    {
        eventAction->totalDataSize = eventAction->totalBytesPassed;
        eventAction->totalBytesPassed = 0;
        size_t n;
        //Log request
        fprintf(Log, "%s", eventAction->URL);

        char *hostname;
        char portNumber[8];
        memset(portNumber, '\0', 8);

        char reqWire[wirelen * 2];

        memset(reqWire, '\0', wirelen);

        char headerTestSubstr[6];
        memcpy(headerTestSubstr, buf, 3);
        headerTestSubstr[3] = '\0';

        printf("Method: '%s'\n", headerTestSubstr);

        if (strcmp(headerTestSubstr, "GET") && strcmp(headerTestSubstr, "get") && strcmp(headerTestSubstr, "Get"))
        {
            //generate bad method response to client
            /*char *responseMessage = "HTTP/1.0 405 Method Not Allowed\r\n\r\n";
            printf("405\n");
            Rio_writen(connfd, responseMessage, strlen(responseMessage));
            Close(connfd);
            continue;*/

            //TODO: create bad response and send to client

            //return NULL;
        }

        unsigned int reqBufPos = 4;

        memcpy(headerTestSubstr, &buf[reqBufPos], 5);
        headerTestSubstr[5] = '\0';

        printf("HTTP protocol: '%s'\n", headerTestSubstr);

        if (strcmp(headerTestSubstr, "http:") && strcmp(headerTestSubstr, "https"))
        {
            //generate malformed request response to client
            /*char *responseMessage = "HTTP/1.0 400 Bad Request\r\n\r\n";
            printf("400\n");
            Rio_writen(connfd, responseMessage, strlen(responseMessage));
            Close(connfd);
            continue;*/

            //TODO: create bad response and send to client
            //return NULL;
        }

        reqBufPos += 7;

        if (!strcmp(headerTestSubstr, "https"))
        {
            printf("https detected. Switching to http\n");

            reqBufPos++;
        }

        unsigned int baseURLlength = 0;
        unsigned int portSpot = 0;
        unsigned int baseURLWithPortLength = 0;

        while (buf[reqBufPos + baseURLlength] != '/' && buf[reqBufPos + baseURLlength] != ' ')
        {
            if (buf[reqBufPos + baseURLlength] == ':')
            {
                portSpot = reqBufPos + baseURLlength;
            }

            baseURLlength++;
        }

        baseURLWithPortLength = baseURLlength;

        int portLength = 0;

        if (portSpot)
        {
            portLength = (reqBufPos + baseURLlength) - (portSpot + 1);

            memcpy(portNumber, &buf[portSpot + 1], portLength);
            portNumber[portLength] = '\0';

            printf("Port specified: '%s'\n", portNumber);

            baseURLlength -= (portLength + 1);
        }

        char baseURL[baseURLlength + 1];

        memcpy(baseURL, &buf[reqBufPos], baseURLlength);
        baseURL[baseURLlength] = '\0';

        printf("Base URL: '%s'\n", baseURL);

        //begin URI, skipping first / in case the user doesn't put it
        if (buf[reqBufPos + baseURLWithPortLength] == '/')
        {
            reqBufPos += baseURLWithPortLength + 1;
        }
        else
        {
            reqBufPos += baseURLWithPortLength;
        }
        size_t URIlength = 0;

        while (buf[reqBufPos + URIlength] != '\r' && buf[reqBufPos + URIlength + 1] != '\n')
        {
            URIlength++;
        }

        URIlength++;

        char URI[URIlength + 1];

        memcpy(URI, &buf[reqBufPos], URIlength);

        URI[URIlength - 1] = '0';
        URI[URIlength] = '\0';

        printf("URI: '%s'\n", URI);

        reqBufPos += URIlength + 2; //skip the \r\n, pointing after \n

        //copy headers
        printf("Initializing request to server...\n");

        char passThroughHeaders[wirelen];

        memset(passThroughHeaders, '\0', wirelen);
        printf("Request initialized.\n");
        strcpy(passThroughHeaders, &buf[reqBufPos]);
        printf("Pass through headers are:\n'%s'\n", passThroughHeaders);

        //determine if required headers are already included
        char *hostHeaderBegin = strstr(passThroughHeaders, "Host:");
        char *connectHeaderBegin = strstr(passThroughHeaders, "Connection:");
        char *proxyConnectHeaderBegin = strstr(passThroughHeaders, "Proxy-Connection:");
        char *userAgentHeaderBegin = strstr(passThroughHeaders, "User-Agent:");

        //begin assembling request string for server endpoint
        strcat(reqWire, "GET /");

        strcat(reqWire, URI);

        strcat(reqWire, "\r\n");

        printf("Building request:\n'%s'\n", reqWire);

        if (!hostHeaderBegin)
        {
            char hostHeader[200];
            memset(hostHeader, '\0', 200);
            strcat(hostHeader, "Host: ");
            strcat(hostHeader, baseURL);
            strcat(reqWire, hostHeader);
            printf("'%s'\n", reqWire);
        }

        if (!connectHeaderBegin)
        {
            strcat(reqWire, connectionHeader);
            printf("'%s'\n", reqWire);
        }

        if (!proxyConnectHeaderBegin)
        {
            strcat(reqWire, proxyHeader);
            printf("'%s'\n", reqWire);
        }

        if (!userAgentHeaderBegin)
        {
            strcat(reqWire, user_agent_hdr);
            printf("'%s'\n", reqWire);
        }

        strcat(reqWire, passThroughHeaders);
        strcat(reqWire, "\r\n\r\n");

        printf("'%s'\n", reqWire);

        hostname = baseURL;

        if (!portSpot)
        {
            strcat(portNumber, "80");
        }

        printf("Port number just before openfd: %s\n", portNumber);

        char buf2[BUFMAX];
        bool inCache = false;

        //When you receive an HTTP request from a client, you will first want to check the cache for the resource corresponding to that URL requested.
        for (int i = 0; i < MAX_CACHE_SIZE; i++)
        {
            if (cache[i])
            {
                if (!strcmp(cache[i]->request, reqWire))
                {
                    inCache = true;
                    printf("Item found in cache\n");
                    strcpy(buf2, cache[i]->data); //This will need to be fixed to run to client from here.
                    n = cache[i]->objectSize;
                    cache[i]->timeLastAccessed = (unsigned long)time(NULL);
                    break;
                }
            }
        }

        //change dataBuff and MAXDATA to cache entries, set bytesSent to 0, change callback function to write to client. change event in epoll to watch for writing and return false.
        if (inCache)
        {
            eventAction->dataBuf = malloc(sizeof(char) * n);
            strcpy(eventAction->dataBuf, buf2);
            eventAction->totalDataSize = n;
            free(eventAction->URL);
            eventAction->URL = NULL;
            eventAction->callback = clwrite;

            struct epoll_event event;
            event.data.ptr = eventAction;
            // add event to epoll file descriptor
            event.events = EPOLLOUT | EPOLLET; //Write mode, use edge-triggered monitoring
            if (epoll_ctl(efd, EPOLL_CTL_MOD, clientfd, &event) < 0)
            {
                fprintf(stderr, "error adding event\n");
                //exit(1);
                return true;
            }
        }
        //If the URL you have requested is not in the cache, then your proxy will need to retrieve it from the upstream server.
        //ADD URL to eventActionStruct, Set bytes passed to 0. create nonblocking server socket and add epoll watch on new server socket. change callback to write to server. Remove read watch on client from epoll and return false;
        else
        {
            int clientfd, rc;
            struct addrinfo hints, *listp, *p;

            /* Get a list of potential server addresses */
            memset(&hints, 0, sizeof(struct addrinfo));
            hints.ai_socktype = SOCK_STREAM; /* Open a connection */
            hints.ai_flags = AI_NUMERICSERV; /* ... using a numeric port arg. */
            hints.ai_flags |= AI_ADDRCONFIG; /* Recommended for connections */

            if ((rc = getaddrinfo(hostname, portNumber, &hints, &listp)) != 0)
            {
                fprintf(stderr, "getaddrinfo failed (%s:%s): %s\n", hostname, portNumber, gai_strerror(rc));
                return -2;
            }

            /* Walk the list for one that we can successfully connect to */
            for (p = listp; p; p = p->ai_next)
            {
                /* Create a socket descriptor */
                if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
                    continue; /* Socket failed, try the next */

                /* Connect to the server */
                if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
                    break; /* Success */
                if (close(clientfd) < 0)
                { /* Connect failed, try another */ //line:netp:openclientfd:closefd
                    fprintf(stderr, "open_clientfd: close failed: %s\n", strerror(errno));
                    return -1;
                }
            }

            /* Clean up */
            freeaddrinfo(listp);
            if (!p) /* All connects failed */
            {
                return -1;
            }

            //make new socket non-blocking
            if (fcntl(clientfd, F_SETFL, fcntl(clientfd, F_GETFL, 0) | O_NONBLOCK) < 0)
            {
                fprintf(stderr, "error setting socket option\n");
                //exit(1);

                //TODO: create error message and send that.
                return false;
            }

            eventAction->serverfd = clientfd;
            eventAction->callback = srvwrite;
            eventAction->dataBuf = malloc(sizeof(char) * strlen(reqWire));

            strcpy(eventAction->dataBuf, reqWire);

            struct epoll_event event;
            event.data.ptr = eventAction;
            // add event to epoll file descriptor
            event.events = EPOLLOUT | EPOLLET; //Write mode, use edge-triggered monitoring

            if (epoll_ctl(efd, EPOLL_CTL_ADD, clientfd, &event) < 0)
            {
                fprintf(stderr, "error adding event\n");
                //TODO: create error message
                //exit(1);
                return true;
            }
            //remove client from watchlist for now...
            if (epoll_ctl(efd, EPOLL_CTL_DEL, eventAction->clientfd, NULL) < 0)
            {
                fprintf(stderr, "error removing event\n");
                //exit(1);
                return true;
            }
        }
    }

    //With I/O multiplexing and non-blocking I/O, you can't loop until you receive (or send) everything; you have to stop when you get an value less than 0 and finish handling the other ready events, after which you will return to the epoll_wait() loop to see if it is ready for more I/O.

    // The difference is that the socket you set up for communication with the server must now be set to non-blocking, just as the listening socket and the client socket are.  And you must register this socket with the epoll instance, for writing, using edge-triggered monitoring.  You can execute connect() immediately, but you cannot initiate the write() call until epoll_wait() indicates that this socket is ready for writing; because the socket is non-blocking, connect() will return before the connection is actually set up. As mentioned earlier, this time you cannot simply begin sending back over the socket; you must wait until the call to epoll_wait() indicates that the socket is available for writing.

    return false;
}

bool srvwrite(event_action *eventAction /*int *clientfd, int *serverfd, size_t *totalBytesPassed, size_t *totalDataSize, unsigned char *dataBuf, char *URL, serviceState *state*/)
{
    int nwritten;

    //With I/O multiplexing and non-blocking I/O, you can't loop until you receive (or send) everything; you have to stop when you get an value less than 0 and finish handling the other ready events, after which you will return to the epoll_wait() loop to see if it is ready for more I/O.
    //When a return value to read() or write() is less than 0 and errno is EAGAIN or EWOULDBLOCK, it is a an indicator that you are done for the moment--but you need to know where you should start next time it's your turn (see man pages for accept and read, and search for blocking).
    //write to server from offset in dataBuf noted by totalPassed and add bytes Sent to total passed. If total passed = total data size, reset buf, maxsize and bytespassed in preparation to recieve data from upstream server. remove old epoll event and add reading epoll event. change callback to read from server
    /*if ((nwritten = write(eventAction->serverfd, eventAction->dataBuf[eventAction->totalBytesPassed], eventAction->totalDataSize - eventAction->totalBytesPassed)) <= 0)
    {
        if (errno == EINTR)  Interrupted by sig handler return */
       /* {
            nwritten = 0;
        }  and call write() again */
       /* else
        {
            //TODO: insert error message here and return to client.
        }
        //return -1;        errno set by write() */
   /* }*/

   while((nwritten = write(eventAction->serverfd, &eventAction->dataBuf[eventAction->totalBytesPassed], eventAction->totalDataSize - eventAction->totalBytesPassed)) > 0)
   {
       eventAction->totalBytesPassed += nwritten;
   }

    //eventAction->totalBytesPassed += nwritten;

    if (eventAction->totalBytesPassed == eventAction->totalDataSize)
    {
        free(eventAction->dataBuf);
        eventAction->dataBuf = NULL;
        eventAction->totalDataSize = 0;
        eventAction->totalBytesPassed = 0;
        eventAction->callback = clwrite;

        struct epoll_event event;
        event.data.ptr = eventAction;
        // add event to epoll file descriptor
        event.events = EPOLLIN | EPOLLET; //read mode, use edge-triggered monitoring

        if (epoll_ctl(efd, EPOLL_CTL_MOD, eventAction->serverfd, &event) < 0)
        {
            fprintf(stderr, "error adding event\n");
            //TODO: create error message
            //exit(1);
            return true;
        }
    }

    else if (errno != EWOULDBLOCK && errno != EAGAIN)
    {
        perror("error reading");
        //TODO: create error mesage and return
        return true;
    }

    return false;
}

bool srvread(event_action *eventAction /*int *clientfd, int *serverfd, size_t *totalBytesPassed, size_t *totalDataSize, unsigned char *dataBuf, char *URL, serviceState *state*/)
{
    size_t len;
    char buf[MAXBUF], tempBuf[MAXBUF];
    memset(buf, '\0', MAXBUF);
    bool endOfReading = false;

    //With I/O multiplexing and non-blocking I/O, you can't loop until you receive (or send) everything; you have to stop when you get an value less than 0 and finish handling the other ready events, after which you will return to the epoll_wait() loop to see if it is ready for more I/O.
    //When a return value to read() or write() is less than 0 and errno is EAGAIN or EWOULDBLOCK, it is a an indicator that you are done for the moment--but you need to know where you should start next time it's your turn (see man pages for accept and read, and search for blocking).
    //read data from socket into buffer at the offset marked by bytespassed. add size of bytesread into total passed. if bytes read = 0 then prep for client writing/ else if flags would block are set then return. else there was an error and you should change buffer and associated data to have an error message and proceed to prep for client writing/
    if (!eventAction->dataBuf)
    {
        eventAction->dataBuf = malloc(sizeof(char) * BUFMAX);
    }

    while ((len = recv(eventAction->serverfd, tempBuf, BUFMAX - eventAction->totalBytesPassed, 0)) > 0)
    {
        strcat(buf, tempBuf);
        eventAction->totalBytesPassed += len;
        //printf("Received %d bytes\n", len);
        //send(connfd, buf, len, 0);
    }
    if (len == 0)
    {
        // EOF received.
        endOfReading = true;
    }
    /*else if (errno == EWOULDBLOCK || errno == EAGAIN)
    {
        // no more data to read() for now
        return false;
    }
    else
    {
        perror("error reading");
        return true;
    }*/
    //When a return value to read() or write() is less than 0 and errno is EAGAIN or EWOULDBLOCK, it is an indicator that you are done for the moment--but you need to know where you should start next time it's your turn
    else if (errno != EWOULDBLOCK && errno != EAGAIN)
    {
        perror("error reading");
        //TODO: create error mesage and return
        return true;
    }

    strcat(eventAction->dataBuf, buf);

    if (endOfReading)
    {
        close(eventAction->serverfd);
        eventAction->serverfd = -1;
        eventAction->totalDataSize = eventAction->totalBytesPassed;
        eventAction->totalBytesPassed = 0;

        eventAction->callback = clwrite;

        struct epoll_event event;
        event.data.ptr = eventAction;
        // add event to epoll file descriptor
        event.events = EPOLLOUT | EPOLLET; //read mode, use edge-triggered monitoring

        if (epoll_ctl(efd, EPOLL_CTL_ADD, eventAction->clientfd, &event) < 0)
        {
            fprintf(stderr, "error adding event\n");
            //exit(1);
            return true;
        }

        //write to cache
        if (eventAction->URL)
        {
            if (eventAction->totalDataSize <= MAX_OBJECT_SIZE)
            //create new entry
            {
                for (int i = 0; i < MAX_CACHE_SIZE; i++)
                {
                    if (!cache[i])
                    {
                        cache[i] = malloc(sizeof(cachentry));
                        //cache[i] = cachentry;

                        strcpy(cache[i]->data, eventAction->dataBuf);
                        cache[i]->objectSize = eventAction->totalDataSize;
                        strcpy(cache[i]->request, eventAction->URL);
                        cache[i]->timeLastAccessed = (unsigned long)time(NULL);
                        currentCacheSize += cache[i]->objectSize;
                        break;
                    }
                }
            }
            else
            {
                //Remove oldest entry from cache and then keep removing until there is enough space
                int lastCache = 0;

                while (!cache[lastCache])
                {
                    lastCache++;
                }

                unsigned long oldestDateTime = cache[lastCache]->timeLastAccessed;

                for (int i = 0; i < MAX_CACHE_SIZE; i++)
                {
                    if (cache[i])
                    {
                        if (cache[i]->timeLastAccessed < oldestDateTime)
                        {
                            lastCache = i;
                            oldestDateTime = cache[i]->timeLastAccessed;
                        }
                    }
                    /*if (!cache[i].objectSize)
                        {
                            continue;
                        }
                        if (cache[i].timeLastAccessed < oldestDateTime)
                        {
                            lastCache = i;
                            oldestDateTime = cache[i].timeLastAccessed;
                        }*/
                }

                memset(&cache[lastCache]->data, '\0', MAX_OBJECT_SIZE);
                memset(&cache[lastCache]->request, '\0', wirelen);
                currentCacheSize -= cache[lastCache]->objectSize;

                strcpy(cache[lastCache]->data, eventAction->dataBuf);
                cache[lastCache]->objectSize = eventAction->totalDataSize;
                strcpy(cache[lastCache]->request, eventAction->URL);
                cache[lastCache]->timeLastAccessed = (unsigned long)time(NULL);
                currentCacheSize += cache[lastCache]->objectSize;

                if (currentCacheSize > MAX_CACHE_SIZE)
                {
                    while (currentCacheSize > MAX_CACHE_SIZE)
                    {
                        lastCache = 0;

                        while (!cache[lastCache])
                        {
                            lastCache++;
                        }

                        oldestDateTime = cache[lastCache]->timeLastAccessed;

                        for (int i = 0; i < MAX_CACHE_SIZE; i++)
                        {
                            if (cache[i])
                            {
                                if (cache[i]->timeLastAccessed < oldestDateTime)
                                {
                                    lastCache = i;
                                    oldestDateTime = cache[i]->timeLastAccessed;
                                }
                            }
                            /*if (!cache[i].objectSize)
                                {
                                    continue;
                                }
                                if (cache[i].timeLastAccessed < oldestDateTime)
                                {
                                    lastCache = i;
                                    oldestDateTime = cache[i].timeLastAccessed;
                                }*/
                        }

                        /*memset(&cache[lastCache].data, '\0', MAX_OBJECT_SIZE);
                            memset(&cache[lastCache].request, '\0', wirelen);
                            cache[lastCache].timeLastAccessed = 0;*/
                        currentCacheSize -= cache[lastCache]->objectSize;
                        //cache[lastCache].objectSize = 0;
                        free(&cache[lastCache]);
                    }
                }
            }
        }
    }
    //to prep for client writing: close file descriptor, free data and set pointer to null. add listeining event to epoll for client fd. if event has URL and is not an error message then write to cache. change callback to write to client.
    return false;
}

bool clwrite(event_action *eventAction /*int *clientfd, int *serverfd, size_t *totalBytesPassed, size_t *totalDataSize, unsigned char *dataBuf, char *URL, serviceState *state*/)
{
    size_t nwritten;
    //With I/O multiplexing and non-blocking I/O, you can't loop until you receive (or send) everything; you have to stop when you get an value less than 0 and finish handling the other ready events, after which you will return to the epoll_wait() loop to see if it is ready for more I/O.
    //When a return value to read() or write() is less than 0 and errno is EAGAIN or EWOULDBLOCK, it is a an indicator that you are done for the moment--but you need to know where you should start next time it's your turn (see man pages for accept and read, and search for blocking).
    /*if ((nwritten = write(eventAction->clientfd, eventAction->dataBuf[eventAction->totalBytesPassed], eventAction->totalDataSize - eventAction->totalBytesPassed)) <= 0)
    {
        if (errno == EINTR) Interrupted by sig handler return */
        /*{
            nwritten = 0;
        } and call write() again */
        /*else
        {
            //TODO: insert error message here and return to client.
        }
        //return -1;        errno set by write() */
    //}
    while((nwritten = write(eventAction->clientfd, &eventAction->dataBuf[eventAction->totalBytesPassed], eventAction->totalDataSize- eventAction->totalBytesPassed)) > 0)
    {
        eventAction->totalBytesPassed += nwritten;
    }

    if(eventAction->totalBytesPassed == eventAction->totalDataSize)
    {
        return true;
    }

    else if (errno != EWOULDBLOCK && errno != EAGAIN)
    {
        perror("error reading");
        //TODO: create error mesage and return
        return true;
    }

    return false;
    /*eventAction->totalBytesPassed += nwritten;
    if (eventAction->totalBytesPassed = eventAction->totalDataSize)
    {
        return true;
    }
    //write to socket from buffer at the offset specified by bytes passed. Add amount sent to bytes passed. if bytes sent = total datasize, return true. if sent results in error, return true. otherwise writing is not done, return false;
    return false;*/
}