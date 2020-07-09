#ifndef __EPOLL_REACTOR_H__
#define __EPOLL_REACTOR_H__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>   
#define BUFFER_LEN 1024
#define MAX_EVENTS 1024

typedef int (*CALLBACK_FUNC)(int fd, int events, void *arg);

struct rwEvent{
    int fd;
    int events;
    void *arg;
    int (*callback)(int fd, int events, void *arg);
	int status;
	char buffer[1024];
	int length;
	long last_active;
    void *data;
};

struct EpollReactor{
    int epfd;
    struct rwEvent *events;
    void *server_inter;
};


void rwEvent_set(struct rwEvent *ev, int fd, CALLBACK_FUNC callback, void *arg);

int rwEvent_add(int epfd, int events, struct rwEvent *ev);

int rwEvent_del(int epfd, struct rwEvent *ev) ;

int epoll_reactor_init(struct EpollReactor *reactor);

void epoll_reactor_destory(struct EpollReactor *reactor);

int epoll_reactor_listener(struct EpollReactor *reactor,int sockfd,CALLBACK_FUNC accept_cb);

int epoll_reactor_run(struct EpollReactor *reactor);

#endif

