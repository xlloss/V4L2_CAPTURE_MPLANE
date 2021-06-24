LIBDRM=/media/slash/project/home/slash/project/DFI/IMX8MP/sdk/5.4-zeus/sysroots/aarch64-poky-linux/usr/include/libdrm
V4L2DEV2=/media/slash/project/home/slash/project/DFI/IMX8MP/sdk/5.4-zeus/sysroots/aarch64-poky-linux/usr/include/imx

v4l2_sample_camera_mp: v4l2_sample_camera_mp.c
	${CC} -O2  v4l2_sample_camera_mp.c -I${LIBDRM} -I${V4L2DEV2} -lpthread -ldrm -o v4l2_sample_camera_mp

clean:
	rm v4l2_sample_camera_mp
