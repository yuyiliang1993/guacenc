#include "epollreactor.h"

void rwEvent_set(struct rwEvent *ev, int fd, CALLBACK_FUNC callback, void *arg) {

	ev->fd = fd;
	ev->callback = callback;
	ev->events = 0;
	ev->arg = arg;
	ev->last_active = time(NULL);
	return ;
}


int rwEvent_add(int epfd, int events, struct rwEvent *ev) {

	struct epoll_event ep_ev = {0, {0}};
	ep_ev.data.ptr = ev;
	ep_ev.events = ev->events = events;
	int op;
	if (ev->status == 1) {
		op = EPOLL_CTL_MOD;
	} else {
		op = EPOLL_CTL_ADD;
		ev->status = 1;
	}
	if (epoll_ctl(epfd, op, ev->fd, &ep_ev) < 0) {
		printf("event add failed [fd=%d], events[%d]\n", ev->fd, events);
		return -1;
	}

	return 0;
}

int rwEvent_del(int epfd, struct rwEvent *ev) {
	struct epoll_event ep_ev = {0, {0}};
	if (ev->status != 1) {
		return -1;
	}
	ep_ev.data.ptr = ev;
	ev->status = 0;
	epoll_ctl(epfd, EPOLL_CTL_DEL, ev->fd, &ep_ev);
	return 0;
}

int epoll_reactor_init(struct EpollReactor *reactor) {
	if (reactor == NULL) 
		return -1;
	memset(reactor, 0, sizeof(struct EpollReactor));

	reactor->epfd = epoll_create(1);
	if (reactor->epfd <= 0) {
		fprintf(stderr,"create epfd in %s err %s\n", __func__, strerror(errno));
		return -2;
	}

	reactor->events = (struct rwEvent*)malloc((MAX_EVENTS) * sizeof(struct rwEvent));
	if (reactor->events == NULL) {
		fprintf(stderr,"create epfd in %s err %s\n", __func__, strerror(errno));
		close(reactor->epfd);
		return -3;
	}
    return 0;
}

void epoll_reactor_destory(struct EpollReactor *reactor) {
    if(reactor){
        if(reactor->epfd > -1)
            close(reactor->epfd);
        if(reactor->events)
            free(reactor->events);
        reactor->epfd = -1;
        reactor->events = NULL;
    }
}


int epoll_reactor_listener(struct EpollReactor *reactor,int sockfd,CALLBACK_FUNC accept_cb) {

	if (reactor == NULL) return -1;
	if (reactor->events == NULL) return -1;
    if(sockfd < 0)  return -1;

	rwEvent_set(&reactor->events[sockfd], sockfd, accept_cb, reactor);
	
	return rwEvent_add(reactor->epfd, EPOLLIN, &reactor->events[sockfd]);
}

int epoll_reactor_run(struct EpollReactor *reactor) {
	if (reactor == NULL) return -1;
	if (reactor->epfd < 0) return -1;
	if (reactor->events == NULL) return -1;
	
	struct epoll_event events[MAX_EVENTS+1];
	int /*checkpos = 0,*/ i;
//	int i = 0;
	while (1) {
#if 0
		long now = time(NULL);
		for (i = 0;i < 100;i ++, checkpos ++) {
			if (checkpos == MAX_EVENTS) {
				checkpos = 0;
			}

			if (reactor->events[checkpos].status != 1) {
				continue;
			}

			long duration = now - reactor->events[checkpos].last_active;

			if (duration >= 60) {
				close(reactor->events[checkpos].fd);
				printf("[fd=%d] timeout\n", reactor->events[checkpos].fd);
				rwEvent_del(reactor->epfd, &reactor->events[checkpos]);
			}
		}

#endif
		int nready = epoll_wait(reactor->epfd, events, MAX_EVENTS, 1000);
		if (nready < 0) {
			perror("epoll_wait");
			continue;
		}
		for (i = 0;i < nready;i ++) {
			struct rwEvent *ev = (struct rwEvent*)events[i].data.ptr;
			if ((events[i].events & EPOLLIN) && (ev->events & EPOLLIN)) {
				ev->callback(ev->fd, events[i].events, ev->arg);
			}
			if ((events[i].events & EPOLLOUT) && (ev->events & EPOLLOUT)) {
				ev->callback(ev->fd, events[i].events, ev->arg);
			}
		}
	}
}

