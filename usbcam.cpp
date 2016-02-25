#define LOG_TAG "usbcam"

// 包含头文件
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <utils/Log.h>
#include "libjpegdecoder/jpegdecoder.h"
#include "usbcam.h"

// 内部函数实现
static int ALIGN(int x, int y) {
  // y must be a power of 2.
  return (x + y - 1) & ~(y - 1);
}

static void* video_render_thread_proc(void *param)
{
    USBCAM *cam = (USBCAM*)param;
    int     err;

    while (1) {
        if (cam->thread_state & (1 << 0)) {
            break;
        }

        if (cam->thread_state & (1 << 1)) {
            usleep(10*1000);
            continue;
        }

        if (cam->update_preview_flag) {
            cam->cur_win = cam->new_win;
            cam->cur_w   = cam->new_w;
            cam->cur_h   = cam->new_h;
            if (cam->cur_win != NULL) {
                native_window_set_usage(cam->cur_win.get(),
                    GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_EXTERNAL_DISP);
                native_window_set_scaling_mode(cam->cur_win.get(), NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
                native_window_set_buffers_geometry(cam->cur_win.get(), cam->cur_w, cam->cur_h, HAL_PIXEL_FORMAT_YV12);
                native_window_set_buffer_count(cam->cur_win.get(), NATIVE_WIN_BUFFER_COUNT);
            }
            cam->update_preview_flag = 0;
        }

        // dequeue camera video buffer
        if (cam->fd > 0) {
            ioctl(cam->fd, VIDIOC_DQBUF, &cam->buf);
        }

//      ALOGD("%d. bytesused: %d, sequence: %d, length = %d\n", cam->buf.index, cam->buf.bytesused,
//              cam->buf.sequence, cam->buf.length);

        {
            int   pts  = (int)(cam->buf.timestamp.tv_usec + cam->buf.timestamp.tv_sec * 1000000);
            char *data = (char*)cam->vbs[cam->buf.index].addr;
            int   len  = cam->buf.bytesused;

            ANativeWindowBuffer *buf;
            if (cam->cur_win != NULL && 0 == native_window_dequeue_buffer_and_wait(cam->cur_win.get(), &buf)) {
                GraphicBufferMapper &mapper = GraphicBufferMapper::get();
                Rect bounds(cam->cur_w, cam->cur_h);
                void *dst = NULL;

                if (0 == mapper.lock(buf->handle, GRALLOC_USAGE_SW_WRITE_OFTEN, bounds, &dst)) {
                    int dst_y_size   = buf->stride * buf->height;
                    int dst_c_stride = ALIGN(buf->stride / 2, 16);
                    int dst_c_size   = dst_c_stride * buf->height / 2;
                    int dst_buf_size = dst_y_size + dst_c_size * 2;

                    if (len) {
                        jpeg_decoder_decode(cam->decoder, data, len, pts);
                        jpeg_decoder_getframe(cam->decoder, dst, cam->cur_w, cam->cur_h);
                    }
                    else {
//                      ALOGW("cam->cur_w = %d, cam->cur_h = %d, dst = %p", cam->cur_w, cam->cur_h, dst);
                        uint8_t *p = (uint8_t*)dst;
                        for (int i=0; i<dst_buf_size/1; i++) {
                            *p++ = rand();
//                          *p++ = 0;
                        }
                        usleep(50* 1000);
                    }
                    mapper.unlock(buf->handle);
                }

                if ((err = cam->cur_win->queueBuffer(cam->cur_win.get(), buf, -1)) != 0) {
                    ALOGW("Surface::queueBuffer returned error %d", err);
                }
            }
        }

        // requeue camera video buffer
        if (cam->fd > 0) {
            ioctl(cam->fd, VIDIOC_QBUF , &cam->buf);
        }
    }

    return NULL;
}

// 函数实现
USBCAM* usbcam_init(const char *dev)
{
    int i;
    struct v4l2_requestbuffers req;
    USBCAM *cam = (USBCAM*)malloc(sizeof(USBCAM));
    if (!cam) {
        return NULL;
    }

    memset(cam, 0, sizeof(USBCAM));
    cam->fd = open(dev, O_RDWR);
    if (cam->fd < 0) {
        ALOGW("failed to open video device: %s\n", dev);
        goto done;
    }

#if 0
    ioctl(cam->fd, VIDIOC_QUERYCAP, &cam->cap);
    ALOGW("\n");
    ALOGW("video device caps \n");
    ALOGW("------------------\n");
    ALOGW("driver:       %s\n" , cam->cap.driver      );
    ALOGW("card:         %s\n" , cam->cap.card        );
    ALOGW("bus_info:     %s\n" , cam->cap.bus_info    );
    ALOGW("version:      %0x\n", cam->cap.version     );
    ALOGW("capabilities: %0x\n", cam->cap.capabilities);
    ALOGW("\n");

    ALOGW("\n");
    ALOGW("VIDIOC_ENUM_FMT   \n");
    ALOGW("------------------\n");
    cam->desc.index = 0;
    cam->desc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (ioctl(cam->fd, VIDIOC_ENUM_FMT, &cam->desc) != -1) {
        ALOGW("%d. flags: %d, description: %-16s, pixelfmt: %d\n",
            cam->desc.index, cam->desc.flags,
            cam->desc.description,
            cam->desc.pixelformat);
        cam->desc.index++;
    }
    ALOGW("\n");
#endif

    cam->fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam->fd, VIDIOC_G_FMT, &cam->fmt);
    cam->fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    ioctl(cam->fd, VIDIOC_S_FMT, &cam->fmt);
    ALOGW("VIDIOC_G_FMT      \n");
    ALOGW("------------------\n");
    ALOGW("width:        %d\n", cam->fmt.fmt.pix.width       );
    ALOGW("height:       %d\n", cam->fmt.fmt.pix.height      );
    ALOGW("pixfmt:       %d\n", cam->fmt.fmt.pix.pixelformat );
    ALOGW("field:        %d\n", cam->fmt.fmt.pix.field       );
    ALOGW("bytesperline: %d\n", cam->fmt.fmt.pix.bytesperline);
    ALOGW("sizeimage:    %d\n", cam->fmt.fmt.pix.sizeimage   );
    ALOGW("colorspace:   %d\n", cam->fmt.fmt.pix.colorspace  );

    req.count  = VIDEO_CAPTURE_BUFFER_COUNT;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(cam->fd, VIDIOC_REQBUFS, &req);

    for (i=0; i<VIDEO_CAPTURE_BUFFER_COUNT; i++) 
    {
        cam->buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cam->buf.memory = V4L2_MEMORY_MMAP;
        cam->buf.index  = i;
        ioctl(cam->fd, VIDIOC_QUERYBUF, &cam->buf);

        cam->vbs[i].addr = mmap(NULL, cam->buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                cam->fd, cam->buf.m.offset);
        cam->vbs[i].len  = cam->buf.length;

        ioctl(cam->fd, VIDIOC_QBUF, &cam->buf);
    }

    // init jpeg decoder
    cam->decoder = jpeg_decoder_init();

done:
    cam->thread_state = (1 << 1);
    pthread_create(&cam->thread_id, NULL, video_render_thread_proc, cam);

    return cam;
}

void usbcam_close(USBCAM *cam)
{
    int i;

    if (!cam) return;

    // wait thread safely exited
    cam->thread_state |= (1 << 0);
    pthread_join(cam->thread_id, NULL);

    // free jpeg decoder
    jpeg_decoder_free(cam->decoder);

    // unmap buffers
    for (i=0; i<VIDEO_CAPTURE_BUFFER_COUNT; i++) {
        munmap(cam->vbs[i].addr, cam->vbs[i].len);
    }

    // close & free
    close(cam->fd);
    free(cam);
}

void usbcam_set_preview_window(USBCAM *cam, const sp<ANativeWindow> win, int w, int h)
{
    if (cam) {
        cam->new_win = win;
        cam->new_w   = w;
        cam->new_h   = h;
        cam->update_preview_flag = 1;
    }
}

void usbcam_set_preview_target(USBCAM *cam, const sp<IGraphicBufferProducer>& gbp, int w, int h)
{
    sp<ANativeWindow> win;
    if (gbp != 0) {
        // Using controlledByApp flag to ensure that the buffer queue remains in
        // async mode for the old camera API, where many applications depend
        // on that behavior.
        win = new Surface(gbp, /*controlledByApp*/ true);
    }
    usbcam_set_preview_window(cam, win, w, h);
}

void usbcam_start_preview(USBCAM *cam)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (cam->fd > 0) {
        ioctl(cam->fd, VIDIOC_STREAMON, &type);
    }

    // start thread
    cam->thread_state &= ~(1 << 1);
}

void usbcam_stop_preview(USBCAM *cam)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (cam->fd > 0) {
        ioctl(cam->fd, VIDIOC_STREAMOFF, &type);
    }

    // pause thread
    cam->thread_state |= ~(1 << 1);
}


