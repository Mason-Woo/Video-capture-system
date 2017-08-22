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
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavutil/mathematics.h>


#include "main.h"

#define VIDEO_WIDTH 640
#define VIDEO_HEIGHT 480
#define VIDEO_FORMAT V4L2_PIX_FMT_MJPEG//V4L2_PIX_FMT_YUYV
#define BUFFER_COUNT 4
#define time_num   1
#define time_den   10

//保存图像缓存的信息
struct buf{
	void *start;
	int len;
};

//保存V4l2的信息
struct v4l2_dev{
	int fd;//设备文件fd
	__u8 name[32];//保存摄像头标签
	__u8 drv[16];//驱动名字
	struct buf *buf;//图片数据的指针
	struct event_ext *ev;//Epoll事件的附加结构
	struct cam *arg;//保存指向cam的指针
};

//保存视频编码和图片编码的信息
struct video_and_jpg{

	int k;  //当前编码的图片帧数
	int got_packet;//延时的帧
	int delayFrame;
};

struct cam
{
	struct v4l2_dev *v4_dev;//v4l2信息
	struct buf tran_frm;//保存图像
	struct video_and_jpg  video;
	__u32 tran_frm_max_size;//传输图像最大值
};

//这个结构贯穿整个视频压缩
AVFormatContext* pFormatCtxEnc;
AVCodecContext* pCodecCtxEnc;
//存放帧原始数据
AVFrame* pFrameEnc;

AVFrame* jpg_picture;
int jpg_y_size;
AVCodecContext* jpg_pCodecCtx;
AVPacket jpg_pkt;
AVStream* jpg_video_st;
AVFormatContext* jpg_pFormatCtx;

unsigned char yuv4200[10000000] = { 0 };
unsigned char yuv4220[10000000] = { 0 };


int yuv422_2_yuv420(unsigned char* yuv420, unsigned char* yuv422, int width,int height) {
	int imgSize = width * height * 2;
	int widthStep422 = width * 2;

	unsigned char* p422 = yuv422;
	unsigned char* p420y = yuv420;
	unsigned char* p420u = yuv420 + imgSize / 2;
	unsigned char* p420v = p420u + imgSize / 8;
	int i, j;
	for (i = 0; i < height; i += 2) {
		p422 = yuv422 + i * widthStep422;
		for (j = 0; j < widthStep422; j += 4) {
			*(p420y++) = p422[j];
			*(p420u++) = p422[j + 1];
			*(p420y++) = p422[j + 2];
		}
		p422 += widthStep422;
		for (j = 0; j < widthStep422; j += 4) {
			*(p420y++) = p422[j];
			*(p420v++) = p422[j + 3];
			*(p420y++) = p422[j + 2];
		}

	}
	return 0;
}

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


/*视频和图片编码函数*/
void code_video(void *arg)
{
	struct cam *v = arg;
	int ret;
	int i=0;

	//把帧缓冲拿到的数据保存在yuv4220里面
	strncpy(yuv4220, v->tran_frm.start,v->tran_frm.len);
	//摄像头采集的是YUV422的图片，而H.264标准的编码需要YUV420的格式，因此要做一个转换
	//这里会耗费很多时间，下次要优化这里
	yuv422_2_yuv420(yuv4200, yuv4220, VIDEO_WIDTH, VIDEO_HEIGHT);

	av_image_alloc(pFrameEnc->data, pFrameEnc->linesize,
			pCodecCtxEnc->width, pCodecCtxEnc->height,
			pCodecCtxEnc->pix_fmt, 1);
	pFrameEnc->data[0] = yuv4200;
	pFrameEnc->data[1] = pFrameEnc->data[0]
			+ pCodecCtxEnc->width * pCodecCtxEnc->height;
	pFrameEnc->data[2] = pFrameEnc->data[1]
			+ pCodecCtxEnc->width * pCodecCtxEnc->height / 4;
	pFrameEnc->linesize[0] = pCodecCtxEnc->width;
	pFrameEnc->linesize[1] = pCodecCtxEnc->width / 2;
	pFrameEnc->linesize[2] = pCodecCtxEnc->width / 2;
	pFrameEnc->pts = (v->video.k + (i - 1) * time_den) * (1000/time_den);
	v->video.k += 1;
	pFrameEnc->width = VIDEO_WIDTH;
	pFrameEnc->height = VIDEO_HEIGHT;

	if (!pFormatCtxEnc->nb_streams) {
		printf("output file does not contain any stream\n");
		exit(0);
	}
	//存储压缩编码数据的结构体
	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;

	printf("encoding frame %d-------", v->video.k);
	
	//编码一帧视频。即将AVFrame（存储YUV像素数据）编码为AVPacket（存储H.264等格式的码流数据）。
	ret = avcodec_encode_video2(pCodecCtxEnc, &pkt, pFrameEnc,&(v->video.got_packet));
	if (ret < 0) {
		av_log(NULL, AV_LOG_FATAL, "Video encoding failed\n");
	}
	if (v->video.got_packet) {
		printf("output frame %d size = %d\n", v->video.k - v->video.delayFrame, pkt.size);
		ret = av_interleaved_write_frame(pFormatCtxEnc, &pkt);
		if (ret != 0) {
			fprintf(stderr, "write frame into file is failed\n");
		} else {
			printf("encode and write one frame success\n");
		}
	} else {
		v->video.delayFrame++;
		printf("no frame output\n");
	}
	av_free_packet(&pkt);	
}

/*事件处理函数*/
void cam_handler(int fd, void *arg)
{
	struct v4l2_buffer buf;
	struct v4l2_dev *v = arg;

	printf("1235545454\n");
	//帧出列
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	ioctl(v->fd, VIDIOC_DQBUF, &buf);

	//保存图像信息
	handle_jpeg_proc(v->buf[buf.index].start, buf.bytesused, v->arg);

	//把采集到的图像编码进视频文件
	//code_video(v->arg);
	

	//从新入队列
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
		
	}
	strcpy((char *)v->name, (char *)cap.card);
	strcpy((char *)v->drv, (char *)cap.driver);

	//设置图像格式
	fmt.type 			= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width 	= VIDEO_WIDTH;
	fmt.fmt.pix.height	= VIDEO_WIDTH;
	fmt.fmt.pix.field	= V4L2_FIELD_INTERLACED;
	fmt.fmt.pix.pixelformat	= VIDEO_FORMAT;

	ioctl(v->fd, VIDIOC_S_FMT, &fmt);

	//申请图像缓冲区
	req.count		= BUFFER_COUNT;
	req.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory		= V4L2_MEMORY_MMAP;
	ioctl(v->fd, VIDIOC_REQBUFS, &req);

	//把内核空间的图像缓冲区映射到用户空间
	v->buf = calloc(BUFFER_COUNT, sizeof(struct buf));

	for(i=0; i < BUFFER_COUNT; i++)
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
	for(i=0; i<BUFFER_COUNT; ++i)
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

/*开始采集*/
void v4l2_start_capture(struct v4l2_dev *v)
{
	enum v4l2_buf_type type;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ioctl(v->fd, VIDIOC_STREAMON, &type);
}


/*ffmpeg编解码初始化*/
void video_encode_init() {

	//视频保存文件名
	char* filename = "./264.flv";
	//存储编解码信息的结构体
	AVCodec* pCodecEnc;
	AVOutputFormat* pOutputFormat;
	//存储每一个音视频流的结构体
	AVStream* video_st;

	int i;
	int ret;//返回信息

	//注册编解码器
	avcodec_register_all();
	//初始化所有组件
	av_register_all();


	//查询和文件名相关的容器
	pOutputFormat = av_guess_format(NULL, filename, NULL);
	if (pOutputFormat == NULL) {
		fprintf(stderr, "Could not guess the format from file\n");
		exit(0);
	} else {
		printf("guess the format from file success\n");
	}

	//为pFormatCtxEnc分配内存
	pFormatCtxEnc = avformat_alloc_context();
	if (pFormatCtxEnc == NULL) {
		fprintf(stderr, "could not allocate AVFormatContex\n");
		exit(0);
	} else {
		printf("allocate AVFormatContext success\n");
	}

	pFormatCtxEnc->oformat = pOutputFormat;
	sprintf(pFormatCtxEnc->filename, "%s", filename);
	printf("filename is %s\n", pFormatCtxEnc->filename);

	//创建一个流通道
	video_st = avformat_new_stream(pFormatCtxEnc, 0);
	if (!video_st) {
		fprintf(stderr, "could not allocate AVstream\n");
		exit(0);
	} else {
		printf("allocate AVstream success\n");
	}
	pCodecCtxEnc = video_st->codec;
	pCodecCtxEnc->codec_id = pOutputFormat->video_codec;
	//采用视频编解码器
	pCodecCtxEnc->codec_type = AVMEDIA_TYPE_VIDEO;
	pCodecCtxEnc->bit_rate = 1000000;
	//表示有多少bit的视频流可以偏移出目前的设定.这里的"设定"是指的cbr或者vbr.
	pCodecCtxEnc->bit_rate_tolerance = 300000000; 
	pCodecCtxEnc->width = VIDEO_WIDTH;
	pCodecCtxEnc->height = VIDEO_HEIGHT;
	//介绍见http://blog.csdn.net/supermanwg/article/details/14521869
	pCodecCtxEnc->time_base = (AVRational) {time_num,time_den};
	//pCodecCtxEnc->time_base.num = 1;
	//pCodecCtxEnc->time_base.den = 25;
	pCodecCtxEnc->pix_fmt = PIX_FMT_YUV420P;
	//??
	pCodecCtxEnc->gop_size = 10;
	pCodecCtxEnc->max_b_frames = 0;

	//为什么设置2次不一样的？
	av_opt_set(pCodecCtxEnc->priv_data, "preset", "superfast", 0);
	av_opt_set(pCodecCtxEnc->priv_data, "tune", "zerolatency", 0);

	//以下的参数完全不知道什么意思
	//运动估计
	pCodecCtxEnc->pre_me = 2;
	//设置最小和最大拉格朗日乘数
	//拉格朗日常数是统计学用来检测瞬间平均值的一种方法
	pCodecCtxEnc->lmin = 10;
	pCodecCtxEnc->lmax = 50;
	//最小和最大量化系数
	pCodecCtxEnc->qmin = 20;
	pCodecCtxEnc->qmax = 80;
	//因为我们的量化系数q是在qmin和qmax之间浮动的，  
	//qblur表示这种浮动变化的变化程度，取值范围0.0～1.0，取0表示不削减  
	pCodecCtxEnc->qblur = 0.0;
	//空间复杂度masking力度，取值范围0.0~1.0
	pCodecCtxEnc->spatial_cplx_masking = 0.3;
	//运动场景预判功能的力度，数值越大编码时间越长 
	pCodecCtxEnc->me_pre_cmp = 2;
	//采用（qmin/qmax的比值来控制码率，1表示局部采用此方法，）  
	pCodecCtxEnc->rc_qsquish = 1;

	//设置 i帧、p帧与B帧之间的量化系数q比例因子，这个值越大，B帧越不清楚  
	//B帧量化系数 = 前一个P帧的量化系数q * b_quant_factor + b_quant_offset  
	pCodecCtxEnc->b_quant_factor = 4.9;
	//i帧、p帧与B帧的量化系数偏移量，偏移越大，B帧越不清楚  
	pCodecCtxEnc->b_quant_offset = 2;
	//p和i的量化系数比例因子，越接近1，P帧越清楚  
	//p的量化系数 = I帧的量化系数 * i_quant_factor + i_quant_offset 
	pCodecCtxEnc->i_quant_factor = 0.1;
	pCodecCtxEnc->i_quant_offset = 0.0;
	//码率控制测率，宏定义，查API  
	pCodecCtxEnc->rc_strategy = 2;
	//b帧的生成策略  
	pCodecCtxEnc->b_frame_strategy = 0;
	//DCT变换算法的设置，有7种设置，这个算法的设置是根据不同的CPU指令集来优化的取值范围在0-7之间  
	pCodecCtxEnc->dct_algo = 0;
	////这两个参数表示对过亮或过暗的场景作masking的力度，0表示不作
	pCodecCtxEnc->lumi_masking = 0.0;
	pCodecCtxEnc->dark_masking = 0.0;

	if (!strcmp(pFormatCtxEnc->oformat->name, "flv")) {
		pCodecCtxEnc->flags |= CODEC_FLAG_GLOBAL_HEADER;
	} else {
		printf("output format is %s\n", pFormatCtxEnc->oformat->name);
	}

	//查找编码器
	pCodecEnc = avcodec_find_encoder(pCodecCtxEnc->codec_id);
	if (!pCodecEnc) {
		fprintf(stderr, "could not find suitable video encoder\n");
		exit(0);
	} else {
		printf("find the encoder success\n");
	}

	//打开编码器
	if (avcodec_open2(pCodecCtxEnc, pCodecEnc, NULL) < 0) {
		fprintf(stderr, "could not open video codec\n");
		exit(0);
	} else {
		printf("open the video codec success\n");
	}

	//为pFrameEnc申请内存
	pFrameEnc = avcodec_alloc_frame();
	if (pFrameEnc == NULL) {
		fprintf(stderr, "could not allocate pFrameEnc\n");
		exit(0);
	} else {
		printf("allocate pFrameEnc success\n");
	}

	//打开输出文件
	ret = avio_open(&pFormatCtxEnc->pb, filename, AVIO_FLAG_WRITE);
	if (ret < 0) {
		fprintf(stderr, "could not open '%s': %s\n", filename, av_err2str(ret));
		exit(0);
	} else {
		printf("open filename = %s success\n", filename);
	}

	//写文件头
	ret = avformat_write_header(pFormatCtxEnc, NULL);
	if (ret < 0) {
		fprintf(stderr, "error occurred when opening outputfile: %s\n",
				av_err2str(ret));
		exit(0);
	} else {
		printf("write the header success\n");
	}


	/*从这里开始是图片编码的部分*/
	char* jpg_filname = "test.jpg";	
	AVOutputFormat* jpg_fmt;	
	AVCodec* jpg_pCodec;
	int jpg_ret;
	uint8_t* jpg_picture_buf;	
	int jpg_size;

	//分配内存
	jpg_pFormatCtx = avformat_alloc_context();
	if (jpg_pFormatCtx == NULL) {
		fprintf(stderr, "could not allocate AVFormatContex\n");
		exit(0);
	} else {
		printf("allocate AVFormatContext success\n");
	}

	//猜测编码器
	jpg_fmt = av_guess_format("mjpeg", NULL, NULL);

	if (jpg_fmt == NULL) {
		fprintf(stderr, "Could not guess the format from file\n");
		exit(0);
	} else {
		printf("guess the format from file success\n");
	}
	jpg_pFormatCtx->oformat = jpg_fmt;
	//打开输出文件
	jpg_ret = avio_open(&jpg_pFormatCtx->pb, jpg_filname, AVIO_FLAG_READ_WRITE);
	if (ret < 0) {
		fprintf(stderr, "could not open '%s': %s\n", jpg_filname, av_err2str(jpg_ret));
		exit(0);
	} else {
		printf("open filename = %s success\n", jpg_filname);
	}	

	//新建一个图像的输出流
	jpg_video_st = avformat_new_stream(jpg_pFormatCtx, 0);
	if (jpg_video_st == NULL){
		printf("create jpeg stream faill\n");
		exit(0);
	}
    jpg_pCodecCtx = jpg_video_st->codec;  
    jpg_pCodecCtx->codec_id = jpg_fmt->video_codec;  
    jpg_pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;  
    jpg_pCodecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;  
  
    jpg_pCodecCtx->width = VIDEO_WIDTH;    
    jpg_pCodecCtx->height = VIDEO_HEIGHT;  
  
    jpg_pCodecCtx->time_base.num = time_num;    
    jpg_pCodecCtx->time_base.den = time_den;     
    //输出一些信息  
    av_dump_format(jpg_pFormatCtx, 0, jpg_filname, 1);

	jpg_pCodec = avcodec_find_encoder(jpg_pCodecCtx->codec_id);  
    if (!jpg_pCodec){  
        printf("Codec not found.");  
        exit(0); 
    }  
    if (avcodec_open2(jpg_pCodecCtx, jpg_pCodec,NULL) < 0){  
        printf("Could not open codec.");  
        exit(0); 
    }  
 	
    jpg_picture = (AVFrame*)avcodec_alloc_frame();  
    jpg_size = avpicture_get_size(jpg_pCodecCtx->pix_fmt, jpg_pCodecCtx->width, jpg_pCodecCtx->height);  
    jpg_picture_buf = (uint8_t *)av_malloc(jpg_size);  
    if (!jpg_picture_buf)  
    {  
    	printf("av_malloc failed\n");
        exit(0); 
    }  
    avpicture_fill((AVPicture *)jpg_picture, jpg_picture_buf, 
    	jpg_pCodecCtx->pix_fmt, jpg_pCodecCtx->width, jpg_pCodecCtx->height);  
  
    //Write Header  
    jpg_ret = avformat_write_header(jpg_pFormatCtx,NULL);  
	if (jpg_ret < 0) {
		fprintf(stderr, "error occurred when opening outputfile: %s\n",
				av_err2str(jpg_ret));
		exit(0);
	} else {
		printf("write the header success\n");
	}
  
    jpg_y_size = jpg_pCodecCtx->width * jpg_pCodecCtx->height;  
    av_new_packet(&jpg_pkt,jpg_y_size*3);
}
/*摄像头子系统初始化*/
struct cam *cam_sys_init()
{
	struct cam *cam;
	cam = calloc(1, sizeof(struct cam));

	//初始化采集子系统
	cam->v4_dev = v4l2_init();
	cam->v4_dev->arg = cam;

	//初始化ffmpeg
	//video_encode_init();
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