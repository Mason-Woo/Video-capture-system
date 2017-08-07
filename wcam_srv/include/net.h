#ifndef __NET_H__
#define __NET_H__

#include <netinet/in.h>
#include "protocol.h"

#define DEF_TCP_SRV_PORT    19868
struct tcp_srv
{
	int sock;
	int epfd;
	void *arg;
};

struct tcp_cli
{
	int sock;//客户机的sockfd
	struct sockaddr_in addr;//客户机的地址

	struct tcp_srv *srv;//保存服务器的相关信息
	struct event_ext *ev_tx;//发送数据的epoll事件
	struct event_ext *ev_rx;//接收数据的epoll事件

	char *buf;
	int len;

	unsigned char req[FRAME_MAX_SZ];//存放请求数据包
	unsigned char rsp[FRAME_MAX_SZ + VID_FRAME_MAX_SZ];//存放发送数据包
};
#endif