LIBDRM=${OECORE_TARGET_SYSROOT}/usr/include/libdrm
V4L2DEV2=${OECORE_TARGET_SYSROOT}/usr/include/imx

v4l2_sample_camera_mp: v4l2_sample_camera_mp.c
	${CC} -O2  v4l2_sample_camera_mp.c -I${LIBDRM} -I${V4L2DEV2} -lpthread -ldrm -o v4l2_sample_camera_mp

clean:
	rm v4l2_sample_camera_mp
