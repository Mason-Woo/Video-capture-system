#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/epoll.h>

#include "main.h"


void net_send(struct tcp_cli *tc, void *buf, int len)
{
	struct tcp_cli *c = tc;
	struct tcp_srv *s = c->srv;

	epoll_del_event(s->epfd, c->ev_rx);
	c->buf = buf;
	c->len = len;
	epoll_add_event(s->epfd, c->ev_tx);
}
static void rx_app_handler(int sock, void *arg)
{
	struct tcp_cli *c = arg;
	int res = 0;
	unsigned char *pbuf;

	pbuf = &c->req[0];
	res = read(c->sock, pbuf, FRAME_HDR_SZ);
	process_incoming(c);
}

static void tx_app_handler(int sock, void *arg)
{
	struct tcp_cli *c = arg;
	struct tcp_srv *s = c->srv;
	int res = 0;

	res = send(sock, c->buf, c->len, 0);
	if(res > 0)
	{
		c->len -= res;
		if(c->len == 0)
		{
			epoll_del_event(s->epfd, c->ev_tx);
			epoll_add_event(s->epfd, c->ev_rx);
		}
	}
}
/*构造返回数据
形参
各种参数

返回值：
响应包的长度*/
int build_ack(unsigned char *rsp, unsigned char type, unsigned char id,
			  unsigned char len, unsigned char *data)
{
	rsp[LEN_POS]	= len;
	rsp[CMD0_POS]	= type;
	rsp[CMD1_POS]	= id;
	memcpy(&rsp[DAT_POS], data, len);

	return len+FRAME_HDR_SZ;
}

struct tcp_srv *net_sys_init()
{
	struct sockaddr_in addr;
	struct sockaddr_in sin;
	struct tcp_srv *s = calloc(1, sizeof(struct tcp_srv));
	int new_sock;
	int len;

	s->epfd = epoll_create(20);
	//socket
	s->sock = socket(AF_INET, SOCK_STREAM, 0);

	//bind
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(DEF_TCP_SRV_PORT);
	bind(s->sock, (struct sockaddr*)&addr, sizeof(struct sockaddr));

	//listen
	len = listen(s->sock, 10);

	srv_main->sock = s->sock;
	// //accept
	// new_sock = accept(s->sock, (struct sockaddr*)&sin, &len);
	// c->sock = new_sock;
	// memcpy(&c->addr, &sin, len);
	// c->srv = s;

	// //将传输子系统的事件加入Epoll池,tcp_cli作为事件的参数
	// c->ev_rx = epoll_event_create(c->sock, EPOLLIN, rx_app_handler, c);
	// c->ev_tx = epoll_event_create(c->sock, EPOLLOUT, tx_app_handler, c);

	// //先加入rx的事件
	// epoll_add_event(c->srv->epfd, c->ev_rx);


	return s;
}

void net_process(void *arg)
{
	//保存sock

	struct epoll_event events[10];	
	int fds;
	int i;
	struct event_ext *e;
	uint32_t event;

	struct tcp_srv *s = arg;

	//申请并初始化一个sock结构
	struct tcp_cli *c = calloc(1, sizeof(struct tcp_cli));

	c->sock = s->new_sock;
	c->srv = srv_main->srv;
	//将传输子系统的事件加入Epoll池,tcp_cli作为事件的参数
	c->ev_rx = epoll_event_create(c->sock, EPOLLIN, rx_app_handler, c);
	c->ev_tx = epoll_event_create(c->sock, EPOLLOUT, tx_app_handler, c);

	//先加入rx的事件
	epoll_add_event(c->srv->epfd, c->ev_rx);

	//等待事件的发生，注意修改图片获取代码
	while(1)
	{
		fds = epoll_wait(c->srv->epfd, events, 10, 1000);
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

	//关闭sock
	close(c->sock);
}