#include "wcam.h"



__u64 clock_get_time_us()
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	return (__u64)ts.tv_sec * 1000000LL + (__u64)ts.tv_nsec / 1000LL;
}


void draw_video_frame(const void *p, int size, void *arg)
{
    struct wcam_win *c      = arg;
    GdkPixbufLoader *pixbuf_loader;
    GdkPixbuf       *pixbuf;
    GdkPixbuf       *pixbuf4fullscreen = NULL;
    GdkScreen       *screen;
    cairo_t         *cr;
    gchar           outstr[100];
    gfloat          fps; 
    guint64         now_us;

    gdk_threads_enter();
    
    /* update frame size label */
    sprintf(outstr, "%d 字节", size);
    gtk_label_set_text(GTK_LABEL(c->frmsize_label), outstr);

    /* update fps label */
    c->frm_cnt++;
    if (c->frm_cnt == 1) {
        c->last_twenty_frm_us = clock_get_time_us();
    } else if (c->frm_cnt > 10) {
        now_us = clock_get_time_us();
        fps = 10.0 * 1000000.0 / (now_us - c->last_twenty_frm_us);
        c->frm_cnt = 0;
        sprintf(outstr, "%2.1f 帧/秒", fps);
        gtk_label_set_text(GTK_LABEL(c->fps_label), outstr);
    }

    /* update video */
    pixbuf_loader = gdk_pixbuf_loader_new(); 		
    gdk_pixbuf_loader_write(pixbuf_loader, p, size, NULL); 
    gdk_pixbuf_loader_close(pixbuf_loader, NULL); 
    pixbuf = gdk_pixbuf_loader_get_pixbuf(pixbuf_loader); 

    /*????cairo*/
    cr = gdk_cairo_create(c->video_area->window);
    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);

    cairo_paint(cr);
    cairo_destroy(cr);

    g_object_unref(pixbuf_loader);
    if (pixbuf4fullscreen)
        g_object_unref(pixbuf4fullscreen);
        
	gdk_threads_leave();

    if (c->need_snapshot) {     /* Maybe we should open a file_save_dialog here */
        char img_path[100];     /* or maybe we should make the image_storage_path Configurable */ 
        FILE *fp;               
        time_t t;
        struct tm *tmp;
        t = time(NULL);
        tmp = localtime(&t);

        strftime(outstr, sizeof(outstr), "%F-%T", tmp);
        printf(SNAPSHOT_PATH"/webcam-%s.jpg\n", outstr);
        printf(SNAPSHOT_PATH"/webcam-%s.jpg", outstr);
        sprintf(img_path, SNAPSHOT_PATH"/webcam-%s.jpg", outstr);
        fp = fopen(img_path, "w");        
        fwrite(p, size, 1, fp);
        fclose(fp);
        c->need_snapshot = FALSE;
    }
}


/*建立和服务器的连接*/
static void connect_handler(GtkButton *button, gpointer data)
{
	struct entry_win *c = data;
	const gchar		*ip;
	gint16			port;

	//获取输入的ip和端口，保存新的socket
	ip = gtk_entry_get_text(GTK_ENTRY(c->ip_entry));
	port = atoi(gtk_entry_get_text(GTK_ENTRY(c->port_entry)));

	c->sock = tcp_init_net(ip, port);

	if(c->sock == -1)
		c->connected = FALSE;
	else
		c->connected = TRUE;
	gtk_main_quit();
}

/*对键盘的响应只支持回车和退出*/
static gboolean key_press_func(GtkWidget* widget, GdkEventKey* key, gpointer data)
{
#define KEY_ENTER 0xff0d
#define KEY_ESC   0xff1b
	struct entry_win *c = data;
	//如果是回车，默认点击OK
	if(KEY_ENTER == key->keyval)
	{
		connect_handler(GTK_BUTTON(c->connect_button), c);
		return TRUE;
	}
	//退出键
	else if(KEY_ESC == key->keyval)
	{
		c->connected = FALSE;
		gtk_main_quit();
		return TRUE;
	}

	return FALSE;
}

static gboolean logo_draw(struct entry_win *c, GtkWidget* box)
{
	gtk_box_pack_start_defaults(GTK_BOX(box), gtk_image_new_from_file(LOGO_IMG));
	gtk_box_pack_start_defaults(GTK_BOX(box), gtk_image_new_from_file(LOGO_IMG1));
	return TRUE;
}

static gboolean entry_area_draw(struct entry_win *c, GtkWidget* box)
{
	GtkWidget *label;	
	GtkWidget *entry;	

	//创建一个文本标签
	label = gtk_label_new("Server Ip:");
    gtk_box_pack_start_defaults(GTK_BOX(box), label);
	
    //创建一个输入框
	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), DEF_CONN_IP);
	gtk_entry_set_max_length(GTK_ENTRY(entry), 15);

    gtk_box_pack_start_defaults(GTK_BOX(box), entry);
    c->ip_entry = entry;	
	

	label = gtk_label_new("Server Port:");
    gtk_box_pack_start_defaults(GTK_BOX(box), label);	
        

	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), DEF_PORT);
	gtk_entry_set_max_length(GTK_ENTRY(entry), 5);

    gtk_box_pack_start_defaults(GTK_BOX(box), entry);	
    c->port_entry = entry;	

	return TRUE;
}

static gboolean button_area_draw(struct entry_win *c, GtkWidget* box)
{
	GtkWidget *button;
	
	//创建ok按钮
	button = gtk_button_new_with_label("OK");  
    gtk_box_pack_start_defaults(GTK_BOX(box), button);
    c->connect_button = button;
	//点击事件关联connect_handler函数
	g_signal_connect(button, "clicked", G_CALLBACK(connect_handler), c);		


	button = gtk_button_new_with_label("Cancel");  
    gtk_box_pack_start_defaults(GTK_BOX(box), button);
	
    g_signal_connect(button, "clicked", G_CALLBACK(gtk_main_quit), NULL);	

	return TRUE;
}
static gboolean entry_win_draw_face(struct entry_win *c)
{
	GtkWidget *vbox;
	GtkWidget *hbox;

	//创建一个垂直组装盒，控件间隔为5个像素
	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_add(GTK_CONTAINER(c->win), vbox);

	//创建一个水平组装盒，控件间隔为5个像素
	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start_defaults(GTK_BOX(vbox), hbox);

	//绘制logo
	logo_draw(c, hbox);

	//绘制IP和端口输入框
	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start_defaults(GTK_BOX(vbox), hbox);
	entry_area_draw(c, hbox);

	//绘制OK和cancel按钮
	hbox = gtk_hbutton_box_new();
	gtk_box_set_spacing(GTK_BOX(hbox), 5);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_END);
    gtk_box_pack_start_defaults(GTK_BOX(vbox), hbox);
    button_area_draw(c, hbox);

    return TRUE;
}
/*创建登录界面*/
entry_win_t login_create()
{
	struct entry_win *c = calloc(1, sizeof(struct entry_win));

	if(!c){
		perror("entry_win_create");
		return NULL;
	}

	c->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	//设置标题，图标，位置等
	gtk_window_set_title(GTK_WINDOW(c->win), WIN_TITLE);
	gtk_window_set_icon(GTK_WINDOW(c->win), gdk_pixbuf_new_from_file(WIN_ICON,NULL));
	gtk_window_set_position(GTK_WINDOW(c->win), GTK_WIN_POS_CENTER);
	//不能改变大小
	gtk_window_set_resizable(GTK_WINDOW(c->win),FALSE);
	//设置窗口大小
	gtk_container_set_border_width(GTK_CONTAINER(c->win), 0);
	//创建关闭串口的处理事件
	g_signal_connect(GTK_OBJECT(c->win),"destroy",G_CALLBACK(gtk_main_quit),NULL);
	//按键输入事件和key_press_event函数关联
	g_signal_connect(G_OBJECT(c->win), "key_press_event",G_CALLBACK(key_press_func), c);
	//允许窗口绘图
	gtk_widget_set_app_paintable(c->win, TRUE);
	//绘制图片和按钮
	entry_win_draw_face(c);
	//显示所有控件
	gtk_widget_show_all(c->win);

	return c;
}

void  main_run()
{
	gtk_main();
}

int login_run(entry_win_t win)
{
	struct entry_win *c = win;

	//等待事件的发生
	gtk_main();
	return c->connected == TRUE ? 0 : -1;
}

int entry_win_get_ip(entry_win_t win, char *ip)
{
	struct entry_win *c = win;
	if(c->connected == FALSE)
		return -1;
	strcpy(ip, gtk_entry_get_text(GTK_ENTRY(c->ip_entry)));
	return 0;
}
int entry_win_get_port(entry_win_t win, char *port)
{
	struct entry_win *c = win;
	if(c->connected == FALSE)
		return -1;
	strcpy(port, gtk_entry_get_text(GTK_ENTRY(c->port_entry)));
	return 0;
}

static gboolean draw_area_draw(struct wcam_win *c, GtkWidget *box)
{
	c->video_area = gtk_drawing_area_new();
	gtk_widget_set_size_request(c->video_area, c->video_width, c->video_height);
	gtk_box_pack_start(GTK_BOX(box), c->video_area, FALSE, FALSE, 0);
	gtk_widget_add_events(c->video_area, GDK_BUTTON_PRESS_MASK);

	return TRUE;
}

static gboolean main_button_area_draw(struct wcam_win *c, GtkWidget *box)
{
	GtkWidget *buttonbox;
	GtkWidget *button;
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *image;

	buttonbox = gtk_hbutton_box_new();
	gtk_box_pack_start(GTK_BOX(box), buttonbox, FALSE, FALSE, 0);
	c->button_area = buttonbox;

	//绘制带勾选控制的按钮
	image = gtk_image_new_from_file(SETTING_BUTTON_IMG);
	label = gtk_label_new("显示控制项");
	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start_defaults(GTK_BOX(hbox), image);
	gtk_box_pack_start_defaults(GTK_BOX(hbox), label);
	button = gtk_check_button_new();
	gtk_container_add(GTK_CONTAINER(button), hbox);
	c->control_area_button = button;
	gtk_box_pack_start_defaults(GTK_BOX(buttonbox), button);

	//绘制普通按钮
	image = gtk_image_new_from_file(SNAP_BUTTON_IMG);
	label = gtk_label_new("快照");
	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start_defaults(GTK_BOX(hbox), image);
	gtk_box_pack_start_defaults(GTK_BOX(hbox), label);
	button = gtk_button_new();
	gtk_container_add(GTK_CONTAINER(button), hbox);
	gtk_box_pack_start_defaults(GTK_BOX(buttonbox), button);

	return TRUE;
}

/*显示窗口信息*/
static gboolean info_area_draw(struct wcam_win *c, GtkWidget *box)
{
	GtkWidget *frame;
	GtkWidget *table;
	GtkWidget *label;
	GtkWidget *align;
	GtkWidget *separator;
	gchar buf[256];

	//新建一个框架，9行2列
	frame = gtk_frame_new("信息区");
	c->info_area = frame;
	gtk_box_pack_start_defaults(GTK_BOX(box), frame);
	table = gtk_table_new(9, 2, FALSE);
	gtk_container_add(GTK_CONTAINER(frame), table);

	//填充主页，版本，服务器等信息
	label = gtk_label_new("主页");
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1,
								GTK_FILL, GTK_SHRINK, 1, 1);
    label = gtk_link_button_new_with_label(WCAM_HOMEPAGE, "http://blog.csdn.net/u013181595");
    align = gtk_alignment_new(0, 0, 0, 0);	            /* 左对齐 */
    gtk_container_add(GTK_CONTAINER(align), label);	
    gtk_table_attach(GTK_TABLE(table), align, 1, 2, 0, 1,
                     GTK_FILL, GTK_SHRINK, 1, 1);

    label = gtk_label_new("版本:");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2,
                     GTK_FILL, GTK_SHRINK, 1, 1);

    label = gtk_label_new(WCAM_VERSION);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 1, 2, 1, 2,
                     GTK_FILL, GTK_SHRINK, 8, 1);

    /* IP & PORT */
    label = gtk_label_new("服务器:");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 2, 3,
                     GTK_FILL, GTK_SHRINK, 1, 1);

    label = gtk_label_new(c->ipaddr);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 1, 2, 2, 3,
                     GTK_FILL, GTK_SHRINK, 8, 1);
    //绘制一条水平线
    separator = gtk_hseparator_new(); 
    gtk_table_attach(GTK_TABLE(table), separator, 0, 1, 3, 4,
                     GTK_FILL, GTK_SHRINK, 1, 1);
    separator = gtk_hseparator_new(); 
    gtk_table_attach(GTK_TABLE(table), separator, 1, 2, 3, 4,
                     GTK_FILL | GTK_EXPAND, GTK_SHRINK, 1, 1);

    /* 绘制帧格式 */
    label = gtk_label_new("帧格式:");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 4, 5,
                     GTK_FILL, GTK_SHRINK, 1, 1);

    sprintf(buf, "%c%c%c%c", (c->video_format)&0xFF,
                             (c->video_format>>8)&0xFF,
                             (c->video_format>>16)&0xFF,
                             (c->video_format>>24)&0xFF);
    label = gtk_label_new(buf);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 1, 2, 4, 5,
                     GTK_FILL, GTK_SHRINK, 8, 1);

    /* 绘制帧尺寸*/
    label = gtk_label_new("帧尺寸:");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 5, 6,
                     GTK_FILL, GTK_SHRINK, 1, 1);

    sprintf(buf, "%d x %d", c->video_width, c->video_height); 
    label = gtk_label_new(buf);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 1, 2, 5, 6,
                     GTK_FILL, GTK_SHRINK, 8, 1);

    /* 绘制帧大小*/
    label = gtk_label_new("帧大小:");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 6, 7,
                     GTK_FILL, GTK_SHRINK, 1, 1);

    label = gtk_label_new("0");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 1, 2, 6, 7,
                     GTK_FILL, GTK_SHRINK, 8, 1);
    c->frmsize_label = label;

    /* 绘制帧速率*/
    label = gtk_label_new("帧速率:");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 7, 8,
                     GTK_FILL, GTK_SHRINK, 1, 1);
    label = gtk_label_new("0");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 1, 2, 7, 8,
                     GTK_FILL, GTK_SHRINK, 8, 1);
    c->fps_label = label;

    /*再绘制一条水平线*/
    separator = gtk_hseparator_new(); 
    gtk_table_attach(GTK_TABLE(table), separator, 0, 1, 8, 9,
                     GTK_FILL, GTK_SHRINK, 1, 1);
    separator = gtk_hseparator_new(); 
    gtk_table_attach(GTK_TABLE(table), separator, 1, 2, 8, 9,
                     GTK_FILL, GTK_SHRINK, 1, 1);

    return TRUE;
}
/*绘制主窗口的控件*/
static gboolean main_win_draw_face(struct wcam_win *c)
{
	GtkWidget *box;
	GtkWidget *hbox;
	GtkWidget *hseparator;

	box = gtk_vbox_new(FALSE, 5);
	gtk_container_add(GTK_CONTAINER(c->win), box);

	//设置图片区域及信息显示区域
	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, TRUE, 0);
	draw_area_draw(c, hbox);
	info_area_draw(c, hbox);

	//绘制水平分割线
	hseparator = gtk_hseparator_new();
	gtk_box_pack_start(GTK_BOX(box), hseparator, FALSE, TRUE, 0);

	//绘制按钮
	main_button_area_draw(c, box);

	return TRUE;
}

void main_quit(GtkWidget *Object, gpointer data)
{
	gtk_main_quit();
}
/*
*主工作页面
*主要实现显示图片和帧率的功能
*/
static gboolean main_create(struct wcam_win *c)
{
	int len;

	c->video_width = 640;
	c->video_height = 480;

	c->video_fullscreen = FALSE;

	//把ip和端口拼接到ipaddr中
	entry_win_get_ip(c->entry_win, c->ipaddr);
	len = strlen(c->ipaddr);
	c->ipaddr[len] = ':';
	entry_win_get_port(c->entry_win, &c->ipaddr[len+1]);

	//创建主工作界面
	c->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(c->win), WIN_TITLE);
	gtk_window_set_icon(GTK_WINDOW(c->win),
									gdk_pixbuf_new_from_file(WIN_ICON, NULL));
	gtk_container_set_border_width(GTK_CONTAINER(c->win), 0);
	g_signal_connect(c->win, "destroy",
					G_CALLBACK(main_quit), c);
	gtk_widget_set_app_paintable(c->win, TRUE);
	main_win_draw_face(c);
	gtk_widget_show_all(c->win);

	gtk_widget_hide(c->win);
	gtk_window_set_position(GTK_WINDOW(c->win), GTK_WIN_POS_CENTER);
	gtk_widget_show(c->win);

	return TRUE;

}

void login_hide(entry_win_t win)
{
	struct entry_win *c = win;
	gtk_widget_hide_all(c->win);
}
gint main(gint argc, gchar* argv[])
{
	int res;

	/*GTK初始化*/
	gtk_init(&argc, &argv);
	g_thread_init(NULL);
	gdk_threads_init();

	struct wcam_win *c = calloc(1,sizeof(struct wcam_win));

	c->entry_win = login_create();
	res = login_run(c->entry_win);
	if(res == -1)
	{
		goto err_win;
	}

	//隐藏登录页面
	login_hide(c->entry_win);

	//创建主工作页面
	net_sys_init(c);
	main_create(c);
	main_run();
err_win:
	free(c->entry_win);
	free(c);

	return 0;
}