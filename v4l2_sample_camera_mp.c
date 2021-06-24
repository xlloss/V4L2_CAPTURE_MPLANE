/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 *  V4L2 Sample
 *  slash.linux.c@gmail.com
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
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define BUFER_REQ 3
#define FRAME_TEST_CNT 500
#define TRY_CNT_LIMIT 100
#define IMAGE_WIDTH  720
#define IMAGE_HEIGHT 240

struct video_data {
	int video_fd;
	FILE *savefile;
	struct video_buffer *vbuf;
};

struct video_buffer {
	void *start;
	size_t length;
	size_t offset;
	int out_height;
	int out_width;
	int dis_x_off;
	int dis_y_off;
};

struct drm_buffer {
	void *fb_base;
	__u32 width;
	__u32 height;
	__u32 stride;
	__u32 size;
	__u32 handle;
	__u32 buf_id;
};

struct drm_device {
	int drm_fd;
	__s32 crtc_id;
	__s32 card_id;
	uint32_t conn_id;
	__u32 bits_per_pixel;
	__u32 bytes_per_pixel;
	drmModeModeInfo mode;
	drmModeCrtc *saved_crtc;

	/* double buffering */
	struct drm_buffer buffers[2];
	__u32 nr_buffer;
	__u32 front_buf;
};

int adjust(__u32 fourcc)
{
	int bpp;

	switch(fourcc) {
		case V4L2_PIX_FMT_XRGB32:
		case V4L2_PIX_FMT_XBGR32:
		case V4L2_PIX_FMT_ARGB32:
		case V4L2_PIX_FMT_ABGR32:
			bpp = 32;
			break;

		case V4L2_PIX_FMT_RGB565:
			bpp = 16;
			break;

		default:
			bpp = 32;
	}

	return bpp;
}

int modeset_find_crtc(struct drm_device *drm,
				drmModeRes *res, drmModeConnector *conn)
{
	int drm_fd = drm->drm_fd;
	int crtc_id, j, i;
	drmModeEncoder *encoder;

	for (i = 0; i < conn->count_encoders; i++) {
		encoder = drmModeGetEncoder(drm_fd, conn->encoders[i]);
		if (!encoder) {
			printf("can't retrieve encoders[%d]\n", i);
			continue;
		}

		for (j = 0; j < res->count_crtcs; j++) {
			if (encoder->possible_crtcs & (1 << j)) {
				crtc_id = res->crtcs[j];
				if (crtc_id > 0) {
					drm->crtc_id = crtc_id;
					drmModeFreeEncoder(encoder);
					return 0;
				}
			}
			crtc_id = -1;
		}

		if (j == res->count_crtcs && crtc_id == -1) {
			printf("cannot find crtc\n");
			drmModeFreeEncoder(encoder);
			continue;
		}
		drmModeFreeEncoder(encoder);
	}
	printf("cannot find suitable CRTC for connector[%d]\n", conn->connector_id);
	return -ENOENT;
}

void drm_destroy_fb(int fd, int index, struct drm_buffer *buf)
{
	struct drm_mode_destroy_dumb dreq;

	munmap(buf->fb_base, buf->size);
	drmModeRmFB(fd, buf->buf_id);

	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = buf->handle;
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
}

int drm_create_fb(int fd, int index, struct drm_buffer *buf)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_destroy_dumb dreq;
	struct drm_mode_map_dumb mreq;
	int ret;

	memset(&creq, 0, sizeof(creq));
	creq.width = buf->width;
	creq.height = buf->height;
	creq.bpp = adjust(V4L2_PIX_FMT_XBGR32);
	/* creq.bpp = adjust(V4L2_PIX_FMT_YUYV); */

	ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
	if (ret < 0) {
		printf("cannot create dumb buffer[%d]\n", index);
		return ret;
	}

	buf->stride = creq.pitch;
	buf->size = creq.size;
	buf->handle = creq.handle;

	ret = drmModeAddFB(fd, buf->width, buf->height, creq.bpp, creq.bpp,
				buf->stride, buf->handle, &buf->buf_id);
	if (ret < 0) {
		printf("Add framebuffer (%d) fail\n", index);
		goto destroy_fb;
	}

	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = buf->handle;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
	if (ret) {
		printf("Map buffer[%d] dump ioctl fail\n", index);
		goto remove_fb;
	}

	buf->fb_base = mmap(0, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED,
							fd, mreq.offset);
	if (buf->fb_base == MAP_FAILED) {
		printf("Cannot mmap dumb buffer[%d]\n", index);
		goto remove_fb;
	}
	memset(buf->fb_base, 0, buf->size);

	return 0;

remove_fb:
	drmModeRmFB(fd, buf->buf_id);

destroy_fb:
	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = buf->handle;
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
	printf("Create DRM buffer[%d] fail\n", index);
	return ret;
}

int modeset_setup_dev(struct drm_device *drm,
				drmModeRes *res, drmModeConnector *conn)
{
	struct drm_buffer *buf = drm->buffers;
	int i, ret;

	ret = modeset_find_crtc(drm, res, conn);
	if (ret < 0)
		return ret;

	memcpy(&drm->mode, &conn->modes[0], sizeof(drm->mode));

	/* double buffering */
	for (i = 0; i < 2; i++) {
		buf[i].width  = conn->modes[0].hdisplay;
		buf[i].height = conn->modes[0].vdisplay;
		ret = drm_create_fb(drm->drm_fd, i, &buf[i]);
		if (ret < 0) {
			while(i)
				drm_destroy_fb(drm->drm_fd, i - 1, &buf[i-1]);

			return ret;
		}

		printf("DRM bufffer[%d] addr=0x%p size=%d w/h=(%d,%d) buf_id=%d\n",
				 i, buf[i].fb_base, buf[i].size,
				 buf[i].width, buf[i].height, buf[i].buf_id);
	}

	drm->bits_per_pixel = adjust(V4L2_PIX_FMT_XBGR32);
	/* drm->bits_per_pixel = adjust(V4L2_PIX_FMT_YUYV); */

	drm->bytes_per_pixel = drm->bits_per_pixel >> 3;

	return 0;
}

int drm_device_prepare(struct drm_device *drm)
{
	drmModeRes *res;
	drmModeConnector *conn;
	int drm_fd = drm->drm_fd;
	int ret, i;

	ret = drmSetMaster(drm_fd);
	if (ret < 0) {
		printf("drmSetMaster fial\n");
		return 0;
	}

	res = drmModeGetResources(drm_fd);
	if (res == NULL) {
		printf("Cannot retrieve DRM resources\n");
		drmDropMaster(drm_fd);
		return -errno;
	}

	/* iterate all connectors */
	for (i = 0; i < res->count_connectors; i++) {
		/* get information for each connector */
		conn = drmModeGetConnector(drm_fd, res->connectors[i]);
		if (!conn) {
			printf("Cannot retrieve DRM connector %u:%u (%d)\n",
				i, res->connectors[i], errno);
			continue;
		}

		/* valid connector? */
		if (conn->connection != DRM_MODE_CONNECTED ||
					conn->count_modes == 0) {
			drmModeFreeConnector(conn);
			continue;
		}

		/* find a valid connector */
		drm->conn_id = conn->connector_id;
		ret = modeset_setup_dev(drm, res, conn);
		if (ret < 0) {
			printf("mode setup device environment fail\n");
			drmDropMaster(drm_fd);
			drmModeFreeConnector(conn);
			drmModeFreeResources(res);
			return ret;
		}

		drmModeFreeConnector(conn);
	}

	drmModeFreeResources(res);
	return 0;
}

int open_drm_device(struct drm_device *drm)
{
	char dev_name[30];
	uint64_t has_dumb;
	int fd, i;

	i = 0;

loop:
	sprintf(dev_name, "/dev/dri/card%d", i++);

	fd = open(dev_name, O_RDWR | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0) {
		printf("Open %s fail\n", dev_name);
		return -1;
	}

	if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 ||
	    !has_dumb) {
		printf("drm device '%s' does not support dumb buffers\n", dev_name);
		close(fd);
		goto loop;
	}

	drm->drm_fd = fd;
	drm->card_id = --i;

	printf("Open %s success\n", dev_name);

	return 0;
}

void close_drm_device(int drm_fd)
{
	if (drm_fd > 0)
		close(drm_fd);
}

int display_on_screen(struct drm_device *drm_dev,
	int video_dev_n, struct video_buffer *buffers)
{
	struct drm_device *drm = drm_dev;
	struct drm_buffer *buf = &drm->buffers[drm->front_buf^1];
	static int enter_count = 0;
	int bufoffset;
	int out_h, out_w, stride_v, stride_d;
	int bytes_per_line;
	int cor_offset_x, cor_offset_y;
	int i, ret;

	cor_offset_x = 0;
	cor_offset_y = 0;

	if (video_dev_n == 1) {
		cor_offset_x = (buf->width >> 1) - (IMAGE_WIDTH >> 1);
		cor_offset_y = (buf->height >> 1) - (IMAGE_HEIGHT >> 1);
		cor_offset_x = (cor_offset_x < 0) ? 0 : cor_offset_x;
		cor_offset_y = (cor_offset_y < 0) ? 0 : cor_offset_y;
	}

	bytes_per_line = drm->bytes_per_pixel * buf->width;

	bufoffset = (buffers->dis_x_off + cor_offset_x) * drm->bytes_per_pixel +
				(buffers->dis_y_off + cor_offset_y) * bytes_per_line;

	out_h = buffers->out_height;
	out_w = buffers->out_width;

	/* V4L2 buffer stride value */
	stride_v = out_w * drm->bytes_per_pixel;

	/* display screen stride value */
	stride_d = out_w * drm->bytes_per_pixel;

	for (i = 1; i < out_h; i++) {
		memcpy(buf->fb_base + bufoffset + i * bytes_per_line,
			buffers->start + i * stride_v, stride_d);
	}

	ret = drmModeSetCrtc(drm->drm_fd, drm->crtc_id, buf->buf_id, 0, 0,
				&drm->conn_id, 1, &drm->mode);
	if (ret < 0) {
		printf("Set Crtc fail\n");
		return ret;
	}

	enter_count = 0;
	drm->front_buf ^= 1;
	return 0;
}

void write_image_file(FILE *file, const void *p, struct v4l2_format *fmt)
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
	struct drm_device *drm;
	struct drm_buffer *drm_buf;
	struct video_data *vdata;
	enum v4l2_buf_type type;
	char *video_file_name[2] = {"video0.xbgr32", "video1.xbgr32"};
	char *video_dev_name[2] = {"/dev/video0", "/dev/video1"};
	char file_output[2][30];
	int i, j, ret, frames_num;
	int retry_cnt, vd_fd;
	int cur_id;
	int out_height, out_width;
	int video_dev_n;

	if (argc > 1)
		video_dev_n = atoi(argv[1]);

	if (video_dev_n != 1 && video_dev_n != 2)
		video_dev_n = 1;

	vdata = malloc(sizeof(struct video_data) * video_dev_n);
	if (!vdata) {
		printf("create vdata fail\n");
		return -EINVAL;
	}


	for (i = 0; i < video_dev_n; i++) {
		sprintf(&file_output[i][0], "%s-%d-%d", video_file_name[i],
			IMAGE_HEIGHT / video_dev_n, IMAGE_WIDTH / video_dev_n);

		vdata[i].savefile = fopen(&file_output[i][0], "w+");
		if (!vdata[i].savefile) {
			printf("file(%d) is null\n", i);
			return -EINVAL;
		}

		vdata[i].video_fd = open(video_dev_name[i], O_RDWR | O_NONBLOCK, 0);
		if (vdata[i].video_fd <= 0) {
			printf("cann't open video(%d) device\n", i);
			return 0;
		}
	}

	drm = malloc(sizeof(*drm));
	if (drm == NULL) {
		printf("alloc DRM device fail\n");
		return -EINVAL;
	}
	memset(drm, 0, sizeof(*drm));

	printf("DRM Open\n");
	ret = open_drm_device(drm);
	if (ret < 0) {
		printf("open_drm_device fail\n");
		return ret;
	}

	printf("DRM Prepare\n");
	ret = drm_device_prepare(drm);
	if (ret < 0) {
		printf("drm_device_prepare fail\n");
		drmDropMaster(drm->drm_fd);
		return ret;
	}

	out_height = IMAGE_HEIGHT;
	out_width = IMAGE_WIDTH;
	if (video_dev_n != 1) {
		out_height = IMAGE_HEIGHT / 2;
		out_width = IMAGE_WIDTH / 2;
	}

	printf("VIDIOC_QUERYCAP\n");
	for (i = 0; i < video_dev_n; i++) {
		ret = ioctl(vdata[i].video_fd, VIDIOC_QUERYCAP, &cap);
		if (!ret) {
			printf("cap = 0x%0x\n", cap.capabilities);
			if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
				printf("cann't support v4l2 capture deivce(%d)\n", i);
				return -EINVAL;
			}
		} else {
			close(vdata[i].video_fd);
			printf("VIDIOC_QUERYCAP(%d) fail\n", i);
			return -EINVAL;
		}

		printf("VIDIOC_ENUM_FMT\n");
		fmtdesc.index = 0;
		fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		while (ioctl(vdata[i].video_fd, VIDIOC_ENUM_FMT, &fmtdesc) != -1) {
			printf("%d.%s\n", fmtdesc.index + 1, fmtdesc.description);
			fmtdesc.index++;
		}

		printf("VIDOC_S_FMT");
		memset(&fmt, 0, sizeof(fmt));
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		/* fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUYV; */
		fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_XBGR32;
		fmt.fmt.pix_mp.width = out_width;
		fmt.fmt.pix_mp.height = out_height; 
		fmt.fmt.pix_mp.num_planes = 1;
		if (ioctl(vdata[i].video_fd, VIDIOC_S_FMT, &fmt) < 0) {
			printf("set format failed\n");
			goto fail;
		}

		printf("VIDIOC_REQBUFS\n");
		memset(&req, 0, sizeof(req));
		req.count = BUFER_REQ;
		req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		req.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(vdata[i].video_fd, VIDIOC_REQBUFS, &req);
		if (ret < 0) {
			printf("VIDIOC_REQBUFS(%d) failed\n", i);
			goto fail;
		}

		buffers = calloc(req.count, sizeof(struct video_buffer));
		if (!buffers) {
			printf("video_buffer(%d) null\n", i);
			goto fail;
		}

		vdata[i].vbuf = buffers;

		printf("VIDIOC_QUERBUF\n");
		for (j = 0; j < BUFER_REQ; j++) {
			vdata[i].vbuf[j].out_height = out_height;
			vdata[i].vbuf[j].out_width = out_width;

			vdata[i].vbuf[j].dis_x_off = 0;
			vdata[i].vbuf[j].dis_y_off = 0;

			/* second camera display offset */
			if (i == 1)
				vdata[i].vbuf[j].dis_x_off = out_width;

			ret =  querybuf(vdata[i].video_fd, j, vdata[i].vbuf);
			if (ret < 0) {
				printf("VIDIOC_QUERBUF error\n");
				goto fail;
			}

			vdata[i].vbuf[j].start = mmap(NULL,
								vdata[i].vbuf[j].length,
								PROT_READ | PROT_WRITE,
								MAP_SHARED,
								vdata[i].video_fd,
								vdata[i].vbuf[j].offset);

			if (vdata[i].vbuf[j].start == MAP_FAILED) {
				printf("vdata[%d]->vbuf[%d] error\n", i, j);
				goto fail;
			}

			printf("VIDIOC_QBUF\n");
			ret =  qbuf(vdata[i].video_fd, j, vdata[i].vbuf);
			if (ret < 0) {
				printf("VIDIOC_QBUF[%d] error\n", i);
				goto fail;
			}
		}

		printf("VIDIOC_STREAMON \n");
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		ret = ioctl(vdata[i].video_fd, VIDIOC_STREAMON, &type);
		if (ret < 0) {
			printf("VIDIOC_STREAMON error\n");
			goto fail;
		}
	}

	//start DRM
	drm_buf = &drm->buffers[drm->front_buf];
	ret = drmModeSetCrtc(drm->drm_fd, drm->crtc_id, drm_buf->buf_id,
						0, 0, &drm->conn_id, 1, &drm->mode);
	if (ret < 0) {
		printf("buffer[%d] set CRTC fail\n", drm_buf->buf_id);
		return ret;
	}

	frames_num = 0;
	retry_cnt = 0;
	while (1) {
		for (i = 0; i < video_dev_n; i++) {
			ret = dqbuf(vdata[i].video_fd, &cur_id);
			if (ret < 0) {
				if (retry_cnt > TRY_CNT_LIMIT) {
					printf("retry_cnt > TRY_CNT_LIMIT\n");
					goto fail;
				}
				retry_cnt++;
				usleep(500);
				continue;
			}

			write_image_file(vdata[i].savefile, vdata[i].vbuf[cur_id].start, &fmt);
			display_on_screen(drm, video_dev_n, &vdata[i].vbuf[cur_id]);

			ret = qbuf(vdata[i].video_fd, cur_id, vdata[i].vbuf);
			if (ret < 0)
				goto fail;

			if (frames_num > FRAME_TEST_CNT) {
				printf("exit\n");
				goto exit;
			}
			printf("frames_num %d\n", frames_num);
			frames_num++;
			retry_cnt = 0;
		}
	}

exit:
fail:
	for (j = 0; j < video_dev_n; j++) {
		close(vdata[j].video_fd);
		fclose(vdata[j].savefile);
	}

	close_drm_device(drm->drm_fd);

	for (j = 0; j < video_dev_n; j++)
		free(vdata[j].vbuf);

	free(vdata);
	free(drm);
	return 0;
}
