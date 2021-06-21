v4l2_sample_camera_mp: v4l2_sample_camera_mp.c
	${CC} -O2  v4l2_sample_camera_mp.c -o v4l2_sample_camera_mp

clean:
	rm v4l2_sample_camera_mp
