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
#include <pthread.h> 
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
#define VIDEO_FORMAT V4L2_PIX_FMT_YUYV//V4L2_PIX_FMT_MJPEG
#define BUFFER_COUNT 4
#define time_num   1
#define time_den   20

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
	int delayFrame;//延时的帧
	
	AVFormatContext* pFormatCtxEnc;//这个结构贯穿整个视频压缩
	AVCodecContext* pCodecCtxEnc;
	AVFrame* pFrameEnc;//存放帧原始数据

	AVFrame* jpg_picture;
	int jpg_y_size;
	AVCodecContext* jpg_pCodecCtx;
	AVPacket jpg_pkt;
	AVStream* jpg_video_st;
	AVFormatContext* jpg_pFormatCtx;

	int got_picture;

	unsigned char yuv4200[10000000];
	unsigned char yuv4220[10000000];

	char* jpg_filname;
};

struct cam
{
	struct v4l2_dev *v4_dev;//v4l2信息
	struct buf tran_frm;//保存图像
	struct video_and_jpg  *video;
	int epfd;//摄像头子系统的epfd
	__u32 tran_frm_max_size;//传输图像最大值
	pthread_rwlock_t rwlock;
};



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
	struct video_and_jpg *video = v->video;
	int ret;
	int i=0;

	//把帧缓冲拿到的数据保存在yuv4220里面
	strncpy(video->yuv4220, v->tran_frm.start,v->tran_frm.len);
	//摄像头采集的是YUV422的图片，而H.264标准的编码需要YUV420的格式，因此要做一个转换
	//这里会耗费很多时间，下次要优化这里
	yuv422_2_yuv420(video->yuv4200, video->yuv4220, VIDEO_WIDTH, VIDEO_HEIGHT);

	av_image_alloc(video->pFrameEnc->data, video->pFrameEnc->linesize,
			video->pCodecCtxEnc->width, video->pCodecCtxEnc->height,
			video->pCodecCtxEnc->pix_fmt, 1);
	video->pFrameEnc->data[0] = video->yuv4200;
	video->pFrameEnc->data[1] = video->pFrameEnc->data[0]
			+ video->pCodecCtxEnc->width * video->pCodecCtxEnc->height;
	video->pFrameEnc->data[2] = video->pFrameEnc->data[1]
			+ video->pCodecCtxEnc->width * video->pCodecCtxEnc->height / 4;
	video->pFrameEnc->linesize[0] = video->pCodecCtxEnc->width;
	video->pFrameEnc->linesize[1] = video->pCodecCtxEnc->width / 2;
	video->pFrameEnc->linesize[2] = video->pCodecCtxEnc->width / 2;
	video->pFrameEnc->pts = (video->k + (i - 1) * time_den) * (1000/time_den);
	video->k += 1;
	video->pFrameEnc->width = VIDEO_WIDTH;
	video->pFrameEnc->height = VIDEO_HEIGHT;

	if (!video->pFormatCtxEnc->nb_streams) {
		printf("output file does not contain any stream\n");
		exit(0);
	}
	//存储压缩编码数据的结构体
	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
	
	//编码一帧视频。即将AVFrame（存储YUV像素数据）编码为AVPacket（存储H.264等格式的码流数据）。
	ret = avcodec_encode_video2(video->pCodecCtxEnc, &pkt, video->pFrameEnc,&(video->got_packet));
	if (ret < 0) {
		av_log(NULL, AV_LOG_FATAL, "Video encoding failed\n");
	}
	if (video->got_packet) {
		//printf("output frame %d size = %d\n", video->k - video->delayFrame, pkt.size);
		ret = av_interleaved_write_frame(video->pFormatCtxEnc, &pkt);
		if (ret != 0) {
			fprintf(stderr, "write frame into file is failed\n");
		} else {
			printf("encoding frame %d success\n", video->k);
			//printf("encode and write one frame success\n");
		}
	} else {
		video->delayFrame++;
		printf("no frame output\n");
	}
	av_free_packet(&pkt);	



	av_new_packet(&video->jpg_pkt,video->jpg_y_size*3);

  
	pthread_rwlock_wrlock (&(v->rwlock));
	//打开输出文件
	ret = avio_open(&video->jpg_pFormatCtx->pb, video->jpg_filname, AVIO_FLAG_READ_WRITE);
	if (ret < 0) {
		fprintf(stderr, "could not open '%s': %s\n", video->jpg_filname, av_err2str(ret));
		exit(0);
	}
	//Write Header  
	ret = avformat_write_header(video->jpg_pFormatCtx,NULL);  
	if (ret < 0) {
		fprintf(stderr, "error occurred when opening outputfile: %s\n",
				av_err2str(ret));
		exit(0);
	}
	//编码一帧图片
	video->jpg_picture->data[0] = video->yuv4200;
	video->jpg_picture->data[1] = video->yuv4200 + video->jpg_y_size;
	video->jpg_picture->data[2] = video->yuv4200 + video->jpg_y_size * 5 / 4;
    //Encode  
    ret = avcodec_encode_video2(video->jpg_pCodecCtx, &video->jpg_pkt,video->jpg_picture,&video->got_picture);  
    if(ret < 0){  
        printf("Encode Error.\n");    
    }  
    if (video->got_picture==1){  
        video->jpg_pkt.stream_index = video->jpg_video_st->index; 
		video->jpg_pkt.pts = 1; 
		video->jpg_video_st->cur_dts = 0;
        ret = av_write_frame(video->jpg_pFormatCtx, &video->jpg_pkt);  
        if (ret < 0)
        	printf("av_write_frame error\n");
    }   
    //Write Trailer  
    av_write_trailer(video->jpg_pFormatCtx);
    avio_close(video->jpg_pFormatCtx->pb);
    pthread_rwlock_unlock(&(v->rwlock));
    av_free_packet(&video->jpg_pkt); 
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

	//把采集到的图像编码进视频文件
	code_video(v->arg);
	
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
struct video_and_jpg *video_encode_init() {

	struct video_and_jpg *video;
	//视频保存文件名
	char* filename = "./264.flv";
	//存储编解码信息的结构体
	AVCodec* pCodecEnc;
	AVOutputFormat* pOutputFormat;
	//存储每一个音视频流的结构体
	AVStream* video_st;

	int i;
	int ret;//返回信息

	video = calloc(1,sizeof(struct video_and_jpg));

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
	video->pFormatCtxEnc = avformat_alloc_context();
	if (video->pFormatCtxEnc == NULL) {
		fprintf(stderr, "could not allocate AVFormatContex\n");
		exit(0);
	} else {
		printf("allocate AVFormatContext success\n");
	}

	video->pFormatCtxEnc->oformat = pOutputFormat;
	sprintf(video->pFormatCtxEnc->filename, "%s", filename);
	printf("filename is %s\n", video->pFormatCtxEnc->filename);

	//创建一个流通道
	video_st = avformat_new_stream(video->pFormatCtxEnc, 0);
	if (!video_st) {
		fprintf(stderr, "could not allocate AVstream\n");
		exit(0);
	} else {
		printf("allocate AVstream success\n");
	}
	video->pCodecCtxEnc = video_st->codec;
	video->pCodecCtxEnc->codec_id = pOutputFormat->video_codec;
	//采用视频编解码器
	video->pCodecCtxEnc->codec_type = AVMEDIA_TYPE_VIDEO;
	video->pCodecCtxEnc->bit_rate = 1000000;
	//表示有多少bit的视频流可以偏移出目前的设定.这里的"设定"是指的cbr或者vbr.
	video->pCodecCtxEnc->bit_rate_tolerance = 300000000; 
	video->pCodecCtxEnc->width = VIDEO_WIDTH;
	video->pCodecCtxEnc->height = VIDEO_HEIGHT;
	//介绍见http://blog.csdn.net/supermanwg/article/details/14521869
	video->pCodecCtxEnc->time_base = (AVRational) {time_num,time_den};
	//pCodecCtxEnc->time_base.num = 1;
	//pCodecCtxEnc->time_base.den = 25;
	video->pCodecCtxEnc->pix_fmt = PIX_FMT_YUV420P;
	//??
	video->pCodecCtxEnc->gop_size = 10;
	video->pCodecCtxEnc->max_b_frames = 0;

	//为什么设置2次不一样的？
	av_opt_set(video->pCodecCtxEnc->priv_data, "preset", "superfast", 0);
	av_opt_set(video->pCodecCtxEnc->priv_data, "tune", "zerolatency", 0);

	//以下的参数完全不知道什么意思
	//运动估计
	video->pCodecCtxEnc->pre_me = 2;
	//设置最小和最大拉格朗日乘数
	//拉格朗日常数是统计学用来检测瞬间平均值的一种方法
	video->pCodecCtxEnc->lmin = 10;
	video->pCodecCtxEnc->lmax = 50;
	//最小和最大量化系数
	video->pCodecCtxEnc->qmin = 20;
	video->pCodecCtxEnc->qmax = 80;
	//因为我们的量化系数q是在qmin和qmax之间浮动的，  
	//qblur表示这种浮动变化的变化程度，取值范围0.0～1.0，取0表示不削减  
	video->pCodecCtxEnc->qblur = 0.0;
	//空间复杂度masking力度，取值范围0.0~1.0
	video->pCodecCtxEnc->spatial_cplx_masking = 0.3;
	//运动场景预判功能的力度，数值越大编码时间越长 
	video->pCodecCtxEnc->me_pre_cmp = 2;
	//采用（qmin/qmax的比值来控制码率，1表示局部采用此方法，）  
	video->pCodecCtxEnc->rc_qsquish = 1;

	//设置 i帧、p帧与B帧之间的量化系数q比例因子，这个值越大，B帧越不清楚  
	//B帧量化系数 = 前一个P帧的量化系数q * b_quant_factor + b_quant_offset  
	video->pCodecCtxEnc->b_quant_factor = 4.9;
	//i帧、p帧与B帧的量化系数偏移量，偏移越大，B帧越不清楚  
	video->pCodecCtxEnc->b_quant_offset = 2;
	//p和i的量化系数比例因子，越接近1，P帧越清楚  
	//p的量化系数 = I帧的量化系数 * i_quant_factor + i_quant_offset 
	video->pCodecCtxEnc->i_quant_factor = 0.1;
	video->pCodecCtxEnc->i_quant_offset = 0.0;
	//码率控制测率，宏定义，查API  
	video->pCodecCtxEnc->rc_strategy = 2;
	//b帧的生成策略  
	video->pCodecCtxEnc->b_frame_strategy = 0;
	//DCT变换算法的设置，有7种设置，这个算法的设置是根据不同的CPU指令集来优化的取值范围在0-7之间  
	video->pCodecCtxEnc->dct_algo = 0;
	////这两个参数表示对过亮或过暗的场景作masking的力度，0表示不作
	video->pCodecCtxEnc->lumi_masking = 0.0;
	video->pCodecCtxEnc->dark_masking = 0.0;

	if (!strcmp(video->pFormatCtxEnc->oformat->name, "flv")) {
		video->pCodecCtxEnc->flags |= CODEC_FLAG_GLOBAL_HEADER;
	} else {
		printf("output format is %s\n", video->pFormatCtxEnc->oformat->name);
	}

	//查找编码器
	pCodecEnc = avcodec_find_encoder(video->pCodecCtxEnc->codec_id);
	if (!pCodecEnc) {
		fprintf(stderr, "could not find suitable video encoder\n");
		exit(0);
	} else {
		printf("find the encoder success\n");
	}

	//打开编码器
	if (avcodec_open2(video->pCodecCtxEnc, pCodecEnc, NULL) < 0) {
		fprintf(stderr, "could not open video codec\n");
		exit(0);
	} else {
		printf("open the video codec success\n");
	}

	//为pFrameEnc申请内存
	video->pFrameEnc = avcodec_alloc_frame();
	if (video->pFrameEnc == NULL) {
		fprintf(stderr, "could not allocate pFrameEnc\n");
		exit(0);
	} else {
		printf("allocate pFrameEnc success\n");
	}

	//打开输出文件
	ret = avio_open(&video->pFormatCtxEnc->pb, filename, AVIO_FLAG_WRITE);
	if (ret < 0) {
		fprintf(stderr, "could not open '%s': %s\n", filename, av_err2str(ret));
		exit(0);
	} else {
		printf("open filename = %s success\n", filename);
	}

	//写文件头
	ret = avformat_write_header(video->pFormatCtxEnc, NULL);
	if (ret < 0) {
		fprintf(stderr, "error occurred when opening outputfile: %s\n",
				av_err2str(ret));
		exit(0);
	} else {
		printf("write the header success\n");
	}


	/*从这里开始是图片编码的部分*/
		
	AVOutputFormat* jpg_fmt;	
	AVCodec* jpg_pCodec;
	int jpg_ret;
	uint8_t* jpg_picture_buf;	
	int jpg_size;

	video->jpg_filname = "test.jpg";
	//分配内存
	video->jpg_pFormatCtx = avformat_alloc_context();
	if (video->jpg_pFormatCtx == NULL) {
		fprintf(stderr, "could not allocate AVFormatContex\n");
		exit(0);
	}

	//猜测编码器
	jpg_fmt = av_guess_format("mjpeg", NULL, NULL);

	if (jpg_fmt == NULL) {
		fprintf(stderr, "Could not guess the format from file\n");
		exit(0);
	}

	video->jpg_pFormatCtx->oformat = jpg_fmt;

	//新建一个图像的输出流
	video->jpg_video_st = avformat_new_stream(video->jpg_pFormatCtx, 0);
	if (video->jpg_video_st == NULL){
		printf("create jpeg stream faill\n");
		exit(0);
	}
    video->jpg_pCodecCtx = video->jpg_video_st->codec;  
    video->jpg_pCodecCtx->codec_id = jpg_fmt->video_codec;  
    video->jpg_pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;  
    video->jpg_pCodecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;  
  
    video->jpg_pCodecCtx->width = VIDEO_WIDTH;    
    video->jpg_pCodecCtx->height = VIDEO_HEIGHT;  
  
    video->jpg_pCodecCtx->time_base.num = time_num;    
    video->jpg_pCodecCtx->time_base.den = time_den;     
    //输出一些信息  
    //av_dump_format(video->jpg_pFormatCtx, 0, jpg_filname, 1);

	jpg_pCodec = avcodec_find_encoder(video->jpg_pCodecCtx->codec_id);  
    if (!jpg_pCodec){  
        printf("Codec not found.");  
        exit(0); 
    }  
    if (avcodec_open2(video->jpg_pCodecCtx, jpg_pCodec,NULL) < 0){  
        printf("Could not open codec.");  
        exit(0); 
    }  
 	
    video->jpg_picture = (AVFrame*)avcodec_alloc_frame();  
    jpg_size = avpicture_get_size(video->jpg_pCodecCtx->pix_fmt, video->jpg_pCodecCtx->width, video->jpg_pCodecCtx->height);  
    jpg_picture_buf = (uint8_t *)av_malloc(jpg_size);  
    if (!jpg_picture_buf)  
    {  
    	printf("av_malloc failed\n");
        exit(0); 
    }  
    avpicture_fill((AVPicture *)video->jpg_picture, jpg_picture_buf, 
	video->jpg_pCodecCtx->pix_fmt, video->jpg_pCodecCtx->width, video->jpg_pCodecCtx->height);  
  
    video->jpg_y_size = video->jpg_pCodecCtx->width * video->jpg_pCodecCtx->height;  

    return video;
}
/*摄像头子系统初始化*/
struct cam *cam_sys_init()
{
	struct cam *cam;
	cam = calloc(1, sizeof(struct cam));

	cam->epfd = epoll_create(20);

	pthread_rwlock_init(&(cam->rwlock),NULL);

	//初始化采集子系统
	cam->v4_dev = v4l2_init();
	cam->v4_dev->arg = cam;

	//初始化ffmpeg
	cam->video = video_encode_init();
	//开始采集
	v4l2_start_capture(cam->v4_dev);

	return cam;
}


/*摄像头线程的运行程序
*参数为摄像头子系统的结构
*/
void cam_process(void *arg)
{
	struct cam *c = arg;
	struct epoll_event events[10];	
	int fds;
	int i;
	struct event_ext *e;
	uint32_t event;

	//往Epoll池中加入事件
	c->v4_dev->ev = epoll_event_create(c->v4_dev->fd, EPOLLIN, cam_handler, c->v4_dev);
	epoll_add_event(c->epfd, c->v4_dev->ev);

	//等待事件发生并处理
	while(1)
	{
		fds = epoll_wait(c->epfd, events, 10, 1000);
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
	//写视频文件尾
	av_write_trailer(c->video->pFormatCtxEnc);	
	if (!(c->video->pFormatCtxEnc->flags & AVFMT_NOFILE))
		avio_close(c->video->pFormatCtxEnc->pb);

 	pthread_rwlock_destroy(&(c->rwlock));
	//释放内存
	free(c);

}
void cam_get_fmt(struct cam *v, __u8 *rsp)
{
	__u32 fmt = V4L2_PIX_FMT_JPEG;
	memcpy(rsp, &fmt, 4);
}

__u32 cam_get_trans_frame(struct cam *v, __u8 *rsp)
{
	//读取文件长度
	struct cam *cam = v;
    __u32 filesize = -1;      
    struct stat statbuff;
    int fd;

	pthread_rwlock_rdlock(&(cam->rwlock));
     
	if(stat("./test.jpg", &statbuff) >= 0)
		filesize = statbuff.st_size;     
	//拷贝数据到rsp中
	fd = open("test.jpg", O_RDONLY);
	read(fd, rsp, filesize);
	close(fd);
 	pthread_rwlock_unlock(&(cam->rwlock));
	// memcpy(rsp, v->tran_frm.start, v->tran_frm.len);
	return filesize;
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

