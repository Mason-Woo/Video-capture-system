#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include "main.h"
struct buf{
	void *start;
	int len;
};

struct v4l2_dev{
	int fd;//设备文件fd
	__u8 name[32];//保存摄像头标签
	__u8 drv[16];//驱动名字
	struct buf *buf;//图片数据的指针
	struct event_ext *ev;//Epoll事件的附加结构
	struct cam *arg;//保存指向cam的指针
};

struct cam
{
	struct v4l2_dev *v4_dev;
	struct buf tran_frm;//保存图像
	__u32 tran_frm_max_size;//传输图像最大值
};

/*保存图像数据，存放在cam->tran_buf中
形参：
p: 图像位置
size：图像大小
arg：需要存放的地方
*/
static void handle_jpeg_proc(void *p, int size, void *arg)
{
	struct cam *v = arg;

	v->tran_frm.start = (void *)p;
	v->tran_frm.len = size;
}
/*事件处理函数*/
void cam_handler(int fd, void *arg)
{
	struct v4l2_buffer buf;
	struct v4l2_dev *v = arg;


	//帧出列
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	ioctl(v->fd, VIDIOC_DQBUF, &buf);

	//保存图像信息
	handle_jpeg_proc(v->buf[buf.index].start, buf.bytesused, v->arg);

	ioctl(v->fd, VIDIOC_QBUF, &buf);
}
/*初始化摄像头*/
struct v4l2_dev *v4l2_init()
{
	struct v4l2_dev *v;
	struct v4l2_capability cap;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers req;
	int i;
	struct v4l2_buffer buf;

	//打开摄像头
	v = calloc(1, sizeof(struct v4l2_dev));
	v->fd = open("/dev/video0",O_RDWR|O_NONBLOCK);

	//获取驱动信息
	ioctl(v->fd, VIDIOC_QUERYCAP, &cap);
	if( !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
	{
		printf("this is not a video device\n");
		return -1;
	}
	strcpy((char *)v->name, (char *)cap.card);
	strcpy((char *)v->drv, (char *)cap.driver);

	//设置图像格式
	fmt.type 			= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width 	= 640;
	fmt.fmt.pix.height	= 480;
	fmt.fmt.pix.field	= V4L2_FIELD_INTERLACED;
	fmt.fmt.pix.pixelformat	= V4L2_PIX_FMT_MJPEG;

	ioctl(v->fd, VIDIOC_S_FMT, &fmt);

	//申请图像缓冲区
	req.count		= 4;
	req.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory		= V4L2_MEMORY_MMAP;
	ioctl(v->fd, VIDIOC_REQBUFS, &req);

	//把内核空间的图像缓冲区映射到用户空间
	v->buf = calloc(req.count, sizeof(struct buf));

	for(i=0; i < req.count; i++)
	{
		//获取缓冲区的信息
		buf.type 	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory 	= V4L2_MEMORY_MMAP;
		buf.index	= i;

		ioctl(v->fd, VIDIOC_QUERYBUF, &buf);

		v->buf[i].len = buf.length;
		v->buf[i].start = mmap(NULL,
								buf.length,
								PROT_READ | PROT_WRITE,
								MAP_SHARED,
								v->fd,
								buf.m.offset);
	}

	//图像缓冲入队列
	for(i=0; i<req.count; ++i)
	{
		buf.type 		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory 		= V4L2_MEMORY_MMAP;
		buf.index 		= i;
		ioctl(v->fd, VIDIOC_QBUF, &buf);
	}
	//往Epoll池中加入事件
	v->ev = epoll_event_create(v->fd, EPOLLIN, cam_handler, v);
	epoll_add_event(srv_main->epfd, v->ev);

	return v;
}

void v4l2_start_capture(struct v4l2_dev *v)
{
	enum v4l2_buf_type type;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ioctl(v->fd, VIDIOC_STREAMON, &type);
}
struct cam *cam_sys_init()
{
	struct cam *cam;
	cam = calloc(1, sizeof(struct cam));

	//初始化采集子系统
	cam->v4_dev = v4l2_init();

	cam->v4_dev->arg = cam;

	//开始采集
	v4l2_start_capture(cam->v4_dev);

	return cam;
}

void cam_get_fmt(struct cam *v, __u8 *rsp)
{
	__u32 fmt = V4L2_PIX_FMT_JPEG;
	memcpy(rsp, &fmt, 4);
}

__u32 cam_get_trans_frame(struct cam *v, __u8 *rsp)
{
	memcpy(rsp, v->tran_frm.start, v->tran_frm.len);
	return v->tran_frm.len;
}
/*请求包处理函数
形参：
c：连接到的客户机的结构*/
int process_incoming(struct tcp_cli *c)
{
	struct cam *v 	= srv_main->cam;
	__u8	*req 	= c->req;
	__u8	*rsp	= c->rsp; 
	__u8	id 		= req[CMD1_POS];
	__u8	fmt_data[FRAME_DAT_MAX];
	__u8	status	= ERR_SUCCESS;
	__u32	pos,len,size;

	switch(id){
		//获取图像格式
		case REQUEST_ID(VID_GET_FMT):
			//获取图像格式
			cam_get_fmt(v, fmt_data);
			//构造返回数据
			build_ack(rsp, (TYPE_SRSP << TYPE_BIT_POS) | SUBS_VID, id, 4, fmt_data);
			//发送返回数据
			net_send(c, rsp, 4 + FRAME_HDR_SZ);
			
			break;

		//获取一帧图像
		case REQUEST_ID(VID_REQ_FRAME):
			pos = FRAME_HDR_SZ + 4;
			//获取一帧图像
			size = cam_get_trans_frame(v, &rsp[pos]);
			//构造返回数据
			build_ack(rsp, (TYPE_SRSP << TYPE_BIT_POS) | SUBS_VID, id, 4, (__u8*)&size);
			//发送返回数据
			net_send(c, rsp, pos + size);
			break;

		default:
			status = ERR_CMD_ID;
			break;
	}

	return status;
}