/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 *  V4L2 Sample
 *  slash.linux.c@gmail.com
 *
 *  Copyright (C) 1999-2012 the contributors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  Alternatively you can redistribute this file under the terms of the
 *  BSD license as stated below:
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *  3. The names of its contributors may not be used to endorse or promote
 *     products derived from this software without specific prior written
 *     permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *  
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
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
