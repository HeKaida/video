#include "v4l2_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


#define CAM_WIDTH      640
#define CAM_HEIGHT     480
#define CAM_PIXFMT     V4L2_PIX_FMT_MJPEG   
#define CAM_BUFFERS    4                   
#define FRAME_TIMEOUT  1000               

int main(int argc, char **argv)
{
	int ret;
	v4l2_capture_t cap;

	if(argc < 2)
	{
		fprintf(stderr, "./* /dev/video*\n");
		return -1;
	}
    
    ret = v4l2_capture_init(&cap, argv[1], CAM_WIDTH, CAM_HEIGHT, CAM_PIXFMT, CAM_BUFFERS);
    if (ret != 0) 
    {
        fprintf(stderr, "V4L2 init fail\n");
        return -1;
    }

    printf("V4L2 init success\n");

    for (int i = 0; i < 10; ++i) 
    {
        ret = v4l2_capture_wait_frame(&cap, FRAME_TIMEOUT);
        if (ret == V4L2_TIMEOUT) 
        {
            fprintf(stderr, "no.%d fps timeout\n", i);
            continue;
        } 
        else if (ret != 0) 
        {
            fprintf(stderr, "wait frame fail! ret:%d\n", ret);
            break;
        }

        int index = -1;
        ret = v4l2_capture_dequeue_buffer(&cap, &index);
        if (ret == 1) 
        {          
            --i;
            continue;
        } 
        else if (ret != 0) 
        {
            fprintf(stderr, "[DQBUF fail! ret=%d\n", ret);
            break;
        }

        printf("index=%d, seq=%u, bytes=%zu\n", index, cap.buffers[index].sequence, cap.buffers[index].length);

        ret = v4l2_capture_queue_buffer(&cap, index);
        if (ret != 0) 
        {
            fprintf(stderr, "QBUF fail! ret: %d\n", ret);
            break;
        }
    }

    v4l2_capture_deinit(&cap);
    printf("done\n");
    
    return 0;
}

