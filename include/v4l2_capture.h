#ifndef V4L2_CAPTURE_H
#define V4L2_CAPTURE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <linux/videodev2.h>

/*
	mmap buffer申请个数
	select监控事件超时事件
*/
#define V4L2_MAX_BUFFERS        4      
#define V4L2_DEFAULT_TIMEOUT_MS 1000  

/*
	---buffer结构体---

	mmap返回的用户空间虚拟地址
	缓冲区长度
	buffer归还内核队列状态
	驱动回填的帧序号
	实际帧数据大小
	驱动回填的采集时间戳
*/
typedef struct {
    void   *start;         
    size_t  length;       
    int     queued;        
    uint32_t sequence;     
    uint32_t bytesused;
    struct timeval timestamp; 
} v4l2_buffer_t;

/*
	---设备配置结构体---

	文件描述符
	buffer状态属性
	图像像素宽度
	图像像素高度
	图像像素格式
	图像总大小
	字节对齐
	buffer缓冲区个数

	前最新帧的buffer索引
	覆盖而丢弃的旧帧个数
*/
typedef struct {
    int             fd;
    enum v4l2_buf_type type;
    uint32_t        width;
    uint32_t        height;
    uint32_t        pixelformat;
    uint32_t        bytesperline;
    uint32_t        sizeimage;
    uint32_t        n_buffers;
    v4l2_buffer_t   buffers[V4L2_MAX_BUFFERS];

    
    int             latest_index;      
    uint64_t        dropped_frames;   
} v4l2_capture_t;

/*初始化设备并启动*/
int  v4l2_capture_init(v4l2_capture_t *cap, const char *dev, uint32_t width, uint32_t height, uint32_t pixelformat, uint32_t buffer_count);


/*超时阻塞等待帧数据*/
int  v4l2_capture_wait_frame(v4l2_capture_t *cap, int timeout_ms);


/*获取帧数据*/
int  v4l2_capture_dequeue_buffer(v4l2_capture_t *cap, int *index);


/*归还帧数据*/
int  v4l2_capture_queue_buffer(v4l2_capture_t *cap, int index);


/*停止设备*/
void v4l2_capture_deinit(v4l2_capture_t *cap);


#define V4L2_TIMEOUT        (-2)
#define V4L2_ERR_IOCTL      (-3)
#define V4L2_ERR_MMAP       (-4)
#define V4L2_ERR_SELECT     (-5)

#endif /* V4L2_CAPTURE_H */
