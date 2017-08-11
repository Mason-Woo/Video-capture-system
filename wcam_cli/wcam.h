#ifndef __WCAM_H__
#define __WCAM_H__

#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>

#define DEF_CONN_IP			"192.168.0.104"
#define DEF_PORT			"19868"
#define LOGO_IMG			"./icons/webcam_icon.png"
#define LOGO_IMG1			"./icons/wcamclient.png"

#define WIN_TITLE			"Web Camera"
#define WIN_ICON			"./icons/icon.png"

#define SNAP_BUTTON_IMG		"./icons/snap-icon-samll.xpm"
#define SETTING_BUTTON_IMG	"./icons/settings.png"

#define WCAM_VERSION		"Web Camera 2.0"
#define WCAM_HOMEPAGE		"http://blog.csdn.net/u013181595"
#define SNAPSHOT_PATH		IMGDIR

#define FRAME_MAX_SZ		253
#define VID_FRMAE_MAX_SZ	(0xFFFFF - FRAME_MAX_SZ)

struct entry_win
{
	GtkWidget	*win;
	GtkWidget	*ip_entry;
	GtkWidget	*port_entry;
	GtkWidget	*connect_button;
	gboolean	connected;
	int			sock;
};

typedef struct entry_win *entry_win_t;
typedef void (*wc_img_porc_t)(const void *p, int size, void *arg);

struct wcam_cli
{
	int 			sock; /* 与服务器通信的套结字 */
	pthread_t		tid;/* 处理线程 */   
	bool			stop;
	__u8			req[FRAME_MAX_SZ];
	__u8			rsp[FRAME_MAX_SZ + VID_FRMAE_MAX_SZ];

	wc_img_porc_t	proc;/* 图像处理函数 */ 
	void 			*arg;/* 图像处理函数传入参数 */
};

typedef struct wcam_cli *wcc_t;

struct wcam_win
{
	GtkWidget		*win;
	wcc_t 			client;
	entry_win_t		entry_win;

	GtkWidget		*video_area;

	guint32			video_format;
	guint32			video_width;
	guint32			video_height;
	gboolean		video_fullscreen;
	gboolean		need_snapshot;

	gchar			ipaddr[24];

	GtkWidget		*fps_label;
	GtkWidget		*frmsize_label;
	guint32			frm_cnt;
	guint64			last_twenty_frm_us;

	GtkWidget		*info_area;
	GtkWidget		*button_area;
	GtkWidget		*control_area_button;
	GtkWidget		*control_area;
};

#endif