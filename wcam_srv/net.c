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

int net_sys_init()
{
	struct sockaddr_in addr;
	struct sockaddr_in sin;
	struct tcp_srv *s = calloc(1, sizeof(struct tcp_srv));
	struct tcp_cli *c = calloc(1, sizeof(struct tcp_cli));

	int new_sock;
	int len;

	//初始化传输子系统
	s->epfd = srv_main->epfd;

	//socket
	s->sock = socket(AF_INET, SOCK_STREAM, 0);

	//bind
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(DEF_TCP_SRV_PORT);
	bind(s->sock, (struct sockaddr*)&addr, sizeof(struct sockaddr));

	//listen
	listen(s->sock, 5);

	//accept
	new_sock = accept(s->sock, (struct sockaddr*)&sin, &len);
	c->sock = new_sock;
	memcpy(&c->addr, &sin, len);
	c->srv = s;

	//将传输子系统的事件加入Epoll池,tcp_cli作为事件的参数
	c->ev_rx = epoll_event_create(c->sock, EPOLLIN, rx_app_handler, c);
	c->ev_tx = epoll_event_create(c->sock, EPOLLOUT, tx_app_handler, c);

	//先加入rx的事件
	epoll_add_event(c->srv->epfd, c->ev_rx);

	//保存数据到srv_main中
	srv_main->srv = s;

	//return s;
	return 0;
}