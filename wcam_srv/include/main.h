#include <stdio.h>
#include "net.h"

struct server
{
	int epfd;//指向创建的Epoll
	struct cam *cam;//指向摄像头子系统
	struct tcp_srv *srv;//指向网络子系统
	struct cfg *cfg;//指向配置子系统
};
struct server *srv_main;
int process_incoming(struct tcp_cli *c);