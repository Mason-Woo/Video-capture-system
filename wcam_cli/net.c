#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/types.h>

#include "protocol.h"
#include "wcam.h"

/*客户端TCP连接初始化*/
int tcp_init_net(char *ip, int port)
{
	int sock;
	struct sockaddr_in addr;

	sock = socket(AF_INET, SOCK_STREAM, 0);

	addr.sin_addr.s_addr = inet_addr(ip);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr)) == -1)
	{
		close(sock);
		return -1;
	}

	return sock;
}

int make_request(__u8 *buf, enum request req, __u8 *dat)
{
	__u32 hdr = req;
	__u8 *p = buf;
	__u8 len;

	memcpy(p, &hdr, FRAME_HDR_SZ);

	p += FRAME_HDR_SZ;

	//查看这个请求包有没有附加数据
	len = REQUEST_LEN(req);
	if(len>0 && dat)
		memcpy(p,dat,len);

	return len + FRAME_HDR_SZ;
}

/*请求图片的线程函数*/
void * video_thread(void *arg)
{
	int len;
	struct wcam_cli *client = (struct wcam_cli *)arg;
	__u8 *rsp = client->rsp;
	int size;

	while(!(client->stop))
	{
		//1、发送图像请求
		//1.1构造图像请求
		len = make_request(client->req, VID_REQ_FRAME, NULL);

		//1.2发送图像请求
		send(client->sock, client->req, len, 0);

		//2、接收图像

		//先接收头部信息,3个字节
		len = FRAME_HDR_SZ;
		recv(client->sock, rsp, len, MSG_WAITALL);//MSG_WAITALL表示需要读全
		
		//接收len
		rsp += FRAME_HDR_SZ;
		len = client->rsp[LEN_POS];
		recv(client->sock, rsp, len, MSG_WAITALL);
		memcpy(&size, rsp, len);//获取len的值，放在size中

		//接收数据
		rsp += len;
		recv(client->sock, rsp, size, MSG_WAITALL);

		//3、把图像交给显示子系统
		draw_video_frame(rsp, size, client->arg);
		//usleep(10000);
	}
}
/*网络系统初始化*/
void net_sys_init(struct wcam_win *c)
{
	pthread_t tid;
	struct wcam_cli *client;

	client  = calloc(1, sizeof(struct wcam_cli));
	client->stop = false;
	client->arg = c;
	client->sock = c->entry_win->sock;
	c->client = client;

	//构建工作线程
	pthread_create(&tid, NULL, video_thread, client);
}