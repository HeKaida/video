#include "v4l2_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>

/*封装ioctl*/
static int xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    
    do 
    {
        r = ioctl(fd, req, arg);
    } 
    while (r == -1 && errno == EINTR);
    
    return r;
}

/*设置图像格式*/
static int setup_format(v4l2_capture_t *cap, uint32_t width, uint32_t height, uint32_t pixelformat)
{
    struct v4l2_format fmt;
    
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                = cap->type;
    fmt.fmt.pix.width       = width;
    fmt.fmt.pix.height      = height;
    fmt.fmt.pix.pixelformat = pixelformat;
    fmt.fmt.pix.field       = V4L2_FIELD_ANY;

    if (xioctl(cap->fd, VIDIOC_S_FMT, &fmt) == -1) 
    {
        perror("VIDIOC_S_FMT");
        return V4L2_ERR_IOCTL;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = cap->type;
    if (xioctl(cap->fd, VIDIOC_G_FMT, &fmt) == -1) 
    {
        perror("VIDIOC_G_FMT");
        return V4L2_ERR_IOCTL;
    }

    cap->width        = fmt.fmt.pix.width;
    cap->height       = fmt.fmt.pix.height;
    cap->pixelformat  = fmt.fmt.pix.pixelformat;
    cap->bytesperline = fmt.fmt.pix.bytesperline;
    cap->sizeimage    = fmt.fmt.pix.sizeimage;

    printf("format: %ux%u, pixfmt=0x%08x, sizeimage=%u\n", cap->width, cap->height, cap->pixelformat, cap->sizeimage);
    
    return 0;
}

/*申请buffer缓冲区*/
static int setup_buffers(v4l2_capture_t *cap, uint32_t buffer_count)
{
    struct v4l2_requestbuffers req;
    
    memset(&req, 0, sizeof(req));
    req.count  = buffer_count;
    req.type   = cap->type;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(cap->fd, VIDIOC_REQBUFS, &req) == -1) {
        perror("VIDIOC_REQBUFS set fail!");
        return V4L2_ERR_IOCTL;
    }
    
    if (req.count < 2) {
        fprintf(stderr, "VIDIOC_REQBUFS no enough mmap buffer: %u\n", req.count);
        return V4L2_ERR_IOCTL;
    }

    cap->n_buffers = req.count;
    for (uint32_t i = 0; i < cap->n_buffers; ++i) 
    {
        struct v4l2_buffer buf;
        
        memset(&buf, 0, sizeof(buf));
        buf.type   = cap->type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (xioctl(cap->fd, VIDIOC_QUERYBUF, &buf) == -1) {
            perror("VIDIOC_QUERYBUF");
            return V4L2_ERR_IOCTL;
        }

        cap->buffers[i].length = buf.length;
        cap->buffers[i].start  = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, cap->fd, buf.m.offset);
        if (cap->buffers[i].start == MAP_FAILED) 
        {
            perror("mmap");
            return V4L2_ERR_MMAP;
        }

        if (xioctl(cap->fd, VIDIOC_QBUF, &buf) == -1) {
            perror("VIDIOC_QBUF init");
            return V4L2_ERR_IOCTL;
        }
        
        cap->buffers[i].queued = 1;
    }

    cap->latest_index = -1;
    cap->dropped_frames = 0;
    
    return 0;
}

/*初始化设备并启动*/
int v4l2_capture_init(v4l2_capture_t *cap, const char *dev, uint32_t width, uint32_t height, uint32_t pixelformat, uint32_t buffer_count)
{
    if (!cap || !dev) 
    	return -1;
    	
    memset(cap, 0, sizeof(*cap));
    cap->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    cap->fd = open(dev, O_RDWR | O_NONBLOCK);
    if (cap->fd == -1) 
    {
        perror("open video device");
        return -1;
    }


    struct v4l2_capability vcap;
    memset(&vcap, 0, sizeof(vcap));
    
    if (xioctl(cap->fd, VIDIOC_QUERYCAP, &vcap) == -1) 
    {
        perror("VIDIOC_QUERYCAP");
        goto fail_close;
    }
    if (!(vcap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) 
    {
        fprintf(stderr, "V4L2_CAP_VIDEO_CAPTURE fail!\n");
        goto fail_close;
    }
    if (!(vcap.capabilities & V4L2_CAP_STREAMING)) 
    {
        fprintf(stderr, "V4L2_CAP_STREAMING fail!\n");
        goto fail_close;
    }

    if (setup_format(cap, width, height, pixelformat) != 0)
        goto fail_close;

    if (buffer_count == 0) buffer_count = 4;
    if (buffer_count > V4L2_MAX_BUFFERS) buffer_count = V4L2_MAX_BUFFERS;

    if (setup_buffers(cap, buffer_count) != 0)
        goto fail_unmap;

    if (xioctl(cap->fd, VIDIOC_STREAMON, &cap->type) == -1) {
        perror("VIDIOC_STREAMON");
        goto fail_unmap;
    }

    return 0;

fail_unmap:

    for (uint32_t i = 0; i < cap->n_buffers; ++i) {
        if (cap->buffers[i].start && cap->buffers[i].start != MAP_FAILED) 
        {
            munmap(cap->buffers[i].start, cap->buffers[i].length);
            cap->buffers[i].start = NULL;
        }
    }
fail_close:

    close(cap->fd);
    cap->fd = -1;
    
    return -1;
}

/*超时阻塞等待帧数据*/
int v4l2_capture_wait_frame(v4l2_capture_t *cap, int timeout_ms)
{
    if (!cap || cap->fd < 0) 
    	return -1;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(cap->fd, &fds);

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int r = select(cap->fd + 1, &fds, NULL, NULL, &tv);
    if (r == -1) 
    {
        if (errno == EINTR) 
        	return 1;  
        perror("select");
        
        return V4L2_ERR_SELECT;
    }
    if (r == 0) 
    {
        fprintf(stderr, "select timeout: %d ms\n", timeout_ms);
        
        return V4L2_TIMEOUT;
    }
    
    return 0; 
}

/*获取帧数据*/
int v4l2_capture_dequeue_buffer(v4l2_capture_t *cap, int *index)
{
    if (!cap || cap->fd < 0 || !index) 
    	return -1;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = cap->type;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(cap->fd, VIDIOC_DQBUF, &buf) == -1) 
    {
        if (errno == EAGAIN) 
        	return 1;  
        	
        perror("VIDIOC_DQBUF");
        
        return V4L2_ERR_IOCTL;
    }

    if (buf.index >= cap->n_buffers) {
        fprintf(stderr, "DQBUF return illegal index: %u\n", buf.index);
        
        return V4L2_ERR_IOCTL;
    }

    cap->buffers[buf.index].queued    = 0;
    cap->buffers[buf.index].sequence  = buf.sequence;
    cap->buffers[buf.index].timestamp = buf.timestamp;
    cap->buffers[buf.index].bytesused = buf.bytesused;
    cap->latest_index = buf.index;
    *index = buf.index;

    return 0;
}

/*归还帧数据*/
int v4l2_capture_queue_buffer(v4l2_capture_t *cap, int index)
{
    if (!cap || cap->fd < 0 || index < 0 || (uint32_t)index >= cap->n_buffers)
        return -1;
    if (cap->buffers[index].queued) 
    	return 0; 
    
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = cap->type;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = index;

    if (xioctl(cap->fd, VIDIOC_QBUF, &buf) == -1) {
        perror("VIDIOC_QBUF");
        return V4L2_ERR_IOCTL;
    }
    cap->buffers[index].queued = 1;
    
    return 0;
}

/*停止设备*/
void v4l2_capture_deinit(v4l2_capture_t *cap)
{
    if (!cap) return;

    if (cap->fd >= 0) 
    {
        xioctl(cap->fd, VIDIOC_STREAMOFF, &cap->type);

        for (uint32_t i = 0; i < cap->n_buffers; ++i) 
        {
            if (cap->buffers[i].start && cap->buffers[i].start != MAP_FAILED) 
            {
                munmap(cap->buffers[i].start, cap->buffers[i].length);
                cap->buffers[i].start = NULL;
            }
        }
        close(cap->fd);
        cap->fd = -1;
    }
    
    cap->n_buffers = 0;
    cap->latest_index = -1;
}

