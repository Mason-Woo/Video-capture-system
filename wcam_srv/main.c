#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/epoll.h>

#include "main.h"


struct event_ext
{
	int fd;//监控文件的fd
	bool epolled;//事件是否在池中的标志
	uint32_t events;//事件类型
	void (*handler)(int fd, void *arg);//处理函数
	void *arg;//处理函数的参数
};

//初始化事件的接口
struct event_ext *epoll_event_create(int fd, uint32_t type, void (*handler)(int , void *), void *arg)
{
	struct event_ext *e = calloc(1,sizeof(struct event_ext));

	e->fd = fd;
	e->events = type;
	e->handler = handler;
	e->arg = arg;

	return e;
}

//添加事件的接口
int epoll_add_event(int epfd, struct event_ext *ev)
{
	struct  epoll_event epv;
	int op;

	//初始化epoll_event
	epv.data.ptr = ev;
	epv.events = ev->events;

	if(ev->epolled)
	{
		op = EPOLL_CTL_MOD;
	}
	else
	{
		op = EPOLL_CTL_ADD;
		ev->epolled = true;
	}

	//将epoll_event加入epoll
	epoll_ctl(epfd, op, ev->fd, &epv);

	return 0;
}

//删除事件
int epoll_del_event(int epfd, struct event_ext *ev)
{
	epoll_ctl(epfd, EPOLL_CTL_DEL, ev->fd, NULL);
	ev->epolled = false;

	return 0;
}


//主函数负责连接新的客户机
//cam负责采集并压缩视频
//net负责各个客户机的连接
int main()
{
	struct epoll_event events[512];	
	int fds;
	int i;
	uint32_t event;
	struct event_ext *e;

	srv_main = calloc(1,sizeof(struct server));

	//创建Epoll
	srv_main->epfd = epoll_create(512);
	//子系统初始化
	srv_main->cam = cam_sys_init();
	srv_main->srv = net_sys_init();

	//等待事件发生并处理
	while(1)
	{
		fds = epoll_wait(srv_main->epfd, events, 512, 1000);
		for(i=0; i<fds; i++)
		{
			event = events[i].events;
			e = events[i].data.ptr;

			if((event & EPOLLIN) && (e->events & EPOLLIN))
			{
				e->handler(e->fd, e->arg);
			}
			if((event & EPOLLOUT) && (e->events & EPOLLOUT))
			{
				e->handler(e->fd, e->arg);
			}
			if((event & EPOLLERR) && (e->events & EPOLLERR))
			{
				e->handler(e->fd, e->arg);
			}
		}
	}

	return -1;
}