#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>

#include "main.h"

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


//主函数负责连接初始化并连接新的客户机
//cam负责采集并压缩视频
//net负责各个客户机的连接
int main()
{
	struct sockaddr_in sin;
	int new_sock;
	int len=0;

	srv_main = calloc(1,sizeof(struct server));

	//初始化线程池
	srv_main->pool = pool_init(10);
	//初始化摄像头子系统
	srv_main->cam = cam_sys_init();
	// //初始化网络子系统
	srv_main->srv = net_sys_init();

	//添加摄像头的任务
	pool_add_task (cam_process, srv_main->cam); 

	while(1)
	{
		//等待客户机连接
		if(( srv_main->srv->new_sock = accept(srv_main->sock, (struct sockaddr*)&sin, &len)) == -1)
		{
			perror("accept:");	
			_exit(0);
		}
		//给线程池添加任务
		pool_add_task(net_process,srv_main->srv);
	}
	//关闭线程池
	pool_destroy();
	//关闭sock
	close(srv_main->srv->sock);
	//释放申请到的内存
	free(srv_main);

	return -1;
}