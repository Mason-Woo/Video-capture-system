#ifndef __MAIN_H_
#define __MAIN_H_

#include <stdio.h>
#include <stdbool.h>
#include "net.h"

struct event_ext{
	int fd;//监控文件的fd
	bool epolled;//事件是否在池中的标志
	uint32_t events;//事件类型
	void (*handler)(int fd, void *arg);//处理函数
	void *arg;//处理函数的参数
};


struct server{
	int sock;
	struct cam *cam;//指向摄像头子系统
	struct tcp_srv *srv;//指向网络子系统
	struct cfg *cfg;//指向配置子系统
	struct Cthread_pool *pool;//指向线程池
};



struct server *srv_main;
int process_incoming(struct tcp_cli *c);
void cam_process(void *arg);
int pool_add_task (void *(*process) (void *arg), void *arg);
void net_process(void *argd);

#endif