#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#define BUFER_REQ 3
#define FRAME_TEST_CNT 100
#define TRY_CNT_LIMIT 100
#define IMAGE_WIDTH  720
#define IMAGE_HEIGHT 240
#define VIDEO_DEV "/dev/video0"
#define FILE_NAME "/tmp/test.yuv"

struct video_buffer {
    void *start;
    size_t  length;
    size_t  offset;
};

void process_image(FILE *file, const void *p, struct v4l2_format *fmt)
{
    fwrite(p, fmt->fmt.pix_mp.plane_fmt[0].sizeimage, 1, file);
    fprintf(stderr, ".");
}

int querybuf(int v4l2_fd, int buf_id, struct video_buffer *buffers)
{
    int ret;
    struct v4l2_buffer buf;
    struct v4l2_plane *planes;

    planes = malloc(1 * sizeof(*planes));
    if (!planes) {
        printf("alloc plane fail\n");
        return -1;
    }

    memset(&buf, 0, sizeof(buf));

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;

    buf.m.planes = planes;
    buf.length = 1;
    buf.index = buf_id;
    ret = ioctl(v4l2_fd, VIDIOC_QUERYBUF, buf);
    if (ret < 0) {
        printf("VIDIOC_QUERBUF error\n");
        return -1;
    }

    buffers[buf_id].length = buf.m.planes[0].length;
    buffers[buf_id].offset = (size_t)buf.m.planes[0].m.mem_offset;

    free(planes);
    return 0;
}

int qbuf(int v4l2_fd, unsigned int buf_id, struct video_buffer *buffers)
{
    int ret;
    struct v4l2_buffer buf;
    struct v4l2_plane *planes;

    planes = malloc(1 * sizeof(*planes));
    if (!planes) {
        printf("alloc plane fail\n");
        return -1;
    }
    memset(&buf, 0, sizeof(buf));

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;

    buf.m.planes = planes;
    buf.length = 1;
    buf.index = buf_id;


    buf.m.planes[0].length = buffers[buf_id].length;
    buf.m.planes[0].m.mem_offset = buffers[buf_id].offset;

    ret = ioctl(v4l2_fd, VIDIOC_QBUF, &buf);
    if (ret < 0) {
        printf("VIDIOC_QBUF error\n");
        return -1;
    }

    free(planes);
    return 0;
}

int dqbuf(int v4l2_fd, unsigned int *buf_id)
{
    int ret;
    struct v4l2_buffer buf;
    struct v4l2_plane *planes;

    planes = malloc(1 * sizeof(*planes));
    if (!planes) {
        printf("alloc plane fail\n");
        return 0;
    }
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length = 1;

    ret = ioctl(v4l2_fd, VIDIOC_DQBUF, &buf);
    if (ret < 0)
        return -1;

    free(planes);

    *buf_id = buf.index;
    return 0;
}

int main(int argc, char *argv[])
{
    struct v4l2_format fmt;
    struct v4l2_fmtdesc fmtdesc;
    struct v4l2_capability cap;
    struct v4l2_requestbuffers req;
    struct video_buffer *buffers;
    struct v4l2_plane planes, *pplanes;
    enum v4l2_buf_type type;
    int i, ret, frames_num;
    int retry_cnt, vd_fd;
    char *device = VIDEO_DEV;
    unsigned int cur_id;
    FILE *file;

    file = fopen(FILE_NAME, "w+");
    if (!file) {
        printf("file is null\n");
        return 0;
    }

    vd_fd = open(device, O_RDWR | O_NONBLOCK, 0);
    if (vd_fd <= 0) {
        printf("cann't open video0\n");
        return 0;
    }

    printf("VIDIOC_QUERYCAP\n");
    ret = ioctl(vd_fd, VIDIOC_QUERYCAP, &cap);
    if (!ret) {
        printf("cap = 0x%0x\n", cap.capabilities);
        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
            printf("cann't support v4l2 capture deivce.\n");
            return 0;
        }
    } else {
        close(vd_fd);
        if (EINVAL == errno)
            fprintf(stderr, "is no V4L2 device\n");

        printf("VIDIOC_QUERYCAP fail.\n");
        return 0;
    }

    printf("\n VIDIOC_ENUM_FMT\n");
    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    while (ioctl(vd_fd, VIDIOC_ENUM_FMT, &fmtdesc) != -1) {
        printf("%d.%s\n", fmtdesc.index + 1, fmtdesc.description);
        fmtdesc.index ++;
    }

    printf("\n VIDOC_S_FMT");
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix_mp.width = IMAGE_WIDTH;
    fmt.fmt.pix_mp.height = IMAGE_HEIGHT; 
    fmt.fmt.pix_mp.num_planes = 1;
    if (ioctl(vd_fd, VIDIOC_S_FMT, &fmt) < 0) {
        printf("set format failed\n");
        goto fail;
    }

    printf("VIDIOC_REQBUFS\n");
    memset(&req, 0, sizeof(req));
    req.count = BUFER_REQ;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(vd_fd, VIDIOC_REQBUFS, &req);
    if (ret < 0) {
        printf("VIDIOC_REQBUFS failed\n");
        goto fail;
    }

    buffers = calloc(req.count, sizeof(struct video_buffer));
    if (!buffers) {
        printf("video_buffer null\n");
        goto fail;
    }

    printf("VIDIOC_REQBUFS\n");
    for (i = 0; i < BUFER_REQ; i++) {
        ret =  querybuf(vd_fd, i, buffers);
        if (ret < 0) {
            printf("VIDIOC_QUERBUF error\n");
            goto fail;
        }

        buffers[i].start = mmap(NULL,
                            buffers[i].length,
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED,
                            vd_fd,
                            buffers[i].offset);

        if (buffers[i].start == MAP_FAILED) {
            printf("buffers error\n");
            goto fail;
        } 
    }

    printf("VIDIOC_QBUF\n");
    for (i = 0; i < BUFER_REQ; i++) {
        ret =  qbuf(vd_fd, i, buffers);
        if (ret < 0) {
            printf("VIDIOC_QBUF error\n");
            goto fail;
        }
    }

    printf("VIDIOC_STREAMON \n");
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ret = ioctl(vd_fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        printf("VIDIOC_STREAMON error\n");
        goto fail;
    }

    frames_num = 0;
    retry_cnt = 0;
    while (1) {
        ret = dqbuf(vd_fd, &cur_id);
        if (ret < 0) {
            if (retry_cnt > TRY_CNT_LIMIT) {
                printf("retry_cnt > TRY_CNT_LIMIT\n");
                goto fail;
            }

            retry_cnt++;
            usleep(300);
            continue;
        }

        process_image(file, buffers[cur_id].start, &fmt);

        ret = qbuf(vd_fd, cur_id, buffers);
        if (ret < 0)
            goto fail;

        if (frames_num > FRAME_TEST_CNT) {
            printf("exit\n");
            break;
        }

        frames_num++;
        retry_cnt = 0;
    }

fail:
    close(vd_fd);
    fclose(file);
    free(buffers);
    return 0;
}
