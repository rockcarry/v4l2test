#define LOG_TAG "camcdr"

// 包含头文件
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <utils/Log.h>
#include "camcdr.h"

// 内部常量定义
#define DO_USE_VAR(v)   do { v = v; } while (0)
#define DEF_WIN_PIX_FMT HAL_PIXEL_FORMAT_RGBX_8888 // HAL_PIXEL_FORMAT_RGBX_8888  // HAL_PIXEL_FORMAT_YCrCb_420_SP

#define CAMCDR_GRALLOC_USAGE GRALLOC_USAGE_SW_READ_NEVER \
                           | GRALLOC_USAGE_SW_WRITE_NEVER \
                           | GRALLOC_USAGE_HW_TEXTURE

// 内部函数实现
static int ALIGN(int x, int y) {
    // y must be a power of 2.
    return (x + y - 1) & ~(y - 1);
}

static void render_v4l2(CAMCDR *cam,
                        void *dstbuf, int dstlen, int dstfmt, int dststride, int dstw, int dsth,
                        void *srcbuf, int srclen, int srcfmt, int srcstride, int srcw, int srch, int pts) {

    DO_USE_VAR(dstlen   );
    DO_USE_VAR(dststride);
    DO_USE_VAR(srcstride);
    DO_USE_VAR(pts      );

    if (dstfmt != DEF_WIN_PIX_FMT) {
        return;
    }

    if (!srclen) {
        uint32_t *dst = (uint32_t*)dstbuf;
        int      size = 0;
        switch (DEF_WIN_PIX_FMT) {
        case HAL_PIXEL_FORMAT_RGB_565:      size = dstw * dsth * 2; break;
        case HAL_PIXEL_FORMAT_RGBX_8888:    size = dstw * dsth * 4; break;
        case HAL_PIXEL_FORMAT_YV12:         size = dstw * dsth + dstw * dsth / 2; break;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP: size = dstw * dsth + dstw * dsth / 2; break;
        }
        while (size > 0) {
            *dst++ = rand();
            size  -= sizeof(uint32_t);
        }
        return;
    }

//  ALOGD("dstfmt = 0x%0x, dststride = %d, dstw = %d, dsth = %d\n", dstfmt, dststride, dstw, dsth);
//  ALOGD("srcfmt = 0x%0x, srcstride = %d, srcw = %d, srch = %d\n", srcfmt, srcstride, srcw, srch);

    if (cam->win_w != dstw || cam->win_h != dsth) {
        AVPixelFormat sws_src_fmt = AV_PIX_FMT_NONE;
        AVPixelFormat sws_dst_fmt = AV_PIX_FMT_NONE;
        switch (srcfmt) {
        case V4L2_PIX_FMT_YUYV: sws_src_fmt = AV_PIX_FMT_YUYV422; break;
        case V4L2_PIX_FMT_NV12: sws_src_fmt = AV_PIX_FMT_NV12;    break;
        case V4L2_PIX_FMT_NV21: sws_src_fmt = AV_PIX_FMT_NV21;    break;
        }
        switch (DEF_WIN_PIX_FMT) {
        case HAL_PIXEL_FORMAT_RGB_565:      sws_dst_fmt = AV_PIX_FMT_RGB565;  break;
        case HAL_PIXEL_FORMAT_RGBX_8888:    sws_dst_fmt = AV_PIX_FMT_BGR32;   break;
        case HAL_PIXEL_FORMAT_YV12:         sws_dst_fmt = AV_PIX_FMT_YUV420P; break;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP: sws_dst_fmt = AV_PIX_FMT_NV21;    break;
        }
        if (cam->swsctxt) {
            sws_freeContext(cam->swsctxt);
        }
        cam->swsctxt = sws_getContext(srcw, srch, sws_src_fmt, dstw, dsth, sws_dst_fmt, SWS_POINT, 0, 0, 0);
        cam->win_w   = dstw;
        cam->win_h   = dsth;
    }

    uint8_t* dst_data[8]     = { (uint8_t*)dstbuf, (uint8_t*)dstbuf + dstw * dsth, (uint8_t*)dstbuf + dstw * dsth };
    uint8_t* src_data[8]     = { (uint8_t*)srcbuf, (uint8_t*)srcbuf + srcw * srch, (uint8_t*)srcbuf + srcw * srch };
    int      dst_linesize[8] = { dstw, dstw / 1, dstw / 1 };
    int      src_linesize[8] = { srcw, srcw / 1, srcw / 1 };
    if (srcfmt == V4L2_PIX_FMT_YUYV) {
        src_linesize[0] = srcw * 2;
    }
    if (dstfmt == HAL_PIXEL_FORMAT_RGBX_8888) {
        dst_linesize[0] = dstw * 4;
    }

    // do sws scale
    sws_scale(cam->swsctxt, src_data, src_linesize, 0, srch, dst_data, dst_linesize);
}

static void* video_render_thread_proc(void *param)
{
    CAMCDR *cam = (CAMCDR*)param;
    int     err;

    while (1) {
        if (cam->thread_state & (1 << 0)) {
            break;
        }

        if (cam->thread_state & (1 << 1)) {
            usleep(10*1000);
            continue;
        }

        if (cam->update_flag) {
            cam->cur_win = cam->new_win;
            if (cam->cur_win != NULL) {
                native_window_set_usage(cam->cur_win.get(), CAMCDR_GRALLOC_USAGE);
                native_window_set_scaling_mode  (cam->cur_win.get(), NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
                native_window_set_buffer_count  (cam->cur_win.get(), NATIVE_WIN_BUFFER_COUNT);
                native_window_set_buffers_format(cam->cur_win.get(), DEF_WIN_PIX_FMT);
            }
            cam->update_flag = 0;
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
                Rect bounds(buf->width, buf->height);
                void *dst = NULL;

                if (0 == mapper.lock(buf->handle, GRALLOC_USAGE_SW_WRITE_OFTEN, bounds, &dst)) {
                    render_v4l2(cam,
                        dst , -1 , buf->format    , buf->stride    , buf->width, buf->height,
                        data, len, cam->cam_pixfmt, cam->cam_stride, cam->cam_w, cam->cam_h, pts);
                    mapper.unlock(buf->handle);
                }

                if ((err = cam->cur_win->queueBuffer(cam->cur_win.get(), buf, -1)) != 0) {
                    ALOGW("Surface::queueBuffer returned error %d\n", err);
                }
            }

            if (!len) {
                usleep(50 * 1000);
            }
        }

        // requeue camera video buffer
        if (cam->fd > 0) {
            ioctl(cam->fd, VIDIOC_QBUF , &cam->buf);
        }
    }

    return NULL;
}

static int v4l2_try_fmt_size(int fd, int fmt, int *width, int *height)
{
    struct v4l2_fmtdesc fmtdesc;
    struct v4l2_format  v4l2fmt;
    int                 find    =  0;
    int                 ret     =  0;

    fmtdesc.index = 0;
    fmtdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ALOGD("\n");
    ALOGD("VIDIOC_ENUM_FMT   \n");
    ALOGD("------------------\n");

    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) != -1) {
        ALOGD("%d. flags: %d, description: %-16s, pixelfmt: 0x%0x\n",
            fmtdesc.index, fmtdesc.flags,
            fmtdesc.description,
            fmtdesc.pixelformat);
        if (fmt == (int)fmtdesc.pixelformat) {
            find = 1;
        }
        fmtdesc.index++;
    }
    ALOGD("\n");

    if (!find) {
        ALOGD("video device can't support pixel format: 0x%0x\n", fmt);
        return -1;
    }

    // try format
    v4l2fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2fmt.fmt.pix.width       = *width;
    v4l2fmt.fmt.pix.height      = *height;
    v4l2fmt.fmt.pix.pixelformat = fmt;
    v4l2fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    ALOGD("\n");
    ALOGD("VIDIOC_ENUM_FRAMESIZES   \n");
    ALOGD("-------------------------\n");

    ret = ioctl(fd, VIDIOC_TRY_FMT, &v4l2fmt);
    if (ret < 0)
    {
        ALOGE("VIDIOC_TRY_FMT Failed: %s\n", strerror(errno));
        return ret;
    }

    // driver surpport this size
    *width  = v4l2fmt.fmt.pix.width;
    *height = v4l2fmt.fmt.pix.height;

    ALOGD("\n");
    ALOGD("using pixel format: 0x%0x\n", fmt);
    ALOGD("using frame size: %d, %d\n\n", *width, *height);
    return 0;
}

// 函数实现
CAMCDR* camcdr_init(const char *dev, int sub, int w, int h)
{
    CAMCDR *cam = (CAMCDR*)malloc(sizeof(CAMCDR));
    if (!cam) {
        return NULL;
    }

    // init context
    memset(cam, 0, sizeof(CAMCDR));

    // open camera device
    cam->fd = open(dev, O_RDWR);
    if (cam->fd < 0) {
        ALOGW("failed to open video device: %s\n", dev);
        goto done;
    }

    struct v4l2_capability cap;
    ioctl(cam->fd, VIDIOC_QUERYCAP, &cap);
    ALOGD("\n");
    ALOGD("video device caps \n");
    ALOGD("------------------\n");
    ALOGD("driver:       %s\n" , cap.driver      );
    ALOGD("card:         %s\n" , cap.card        );
    ALOGD("bus_info:     %s\n" , cap.bus_info    );
    ALOGD("version:      %0x\n", cap.version     );
    ALOGD("capabilities: %0x\n", cap.capabilities);
    ALOGD("\n");

    if (strcmp((char*)cap.driver, "uvcvideo") != 0) {
        struct v4l2_input input;
        input.index = sub;
        ioctl(cam->fd, VIDIOC_S_INPUT, &input);
    }

    if (0 == v4l2_try_fmt_size(cam->fd, V4L2_PIX_FMT_NV12, &w, &h)) {
        cam->cam_pixfmt = V4L2_PIX_FMT_NV12;
        cam->cam_w      = w;
        cam->cam_h      = h;
    }
    else if (0 == v4l2_try_fmt_size(cam->fd, V4L2_PIX_FMT_NV21, &w, &h)) {
        cam->cam_pixfmt = V4L2_PIX_FMT_NV21;
        cam->cam_w      = w;
        cam->cam_h      = h;
    }
    else if (0 == v4l2_try_fmt_size(cam->fd, V4L2_PIX_FMT_YUYV, &w, &h)) {
        cam->cam_pixfmt = V4L2_PIX_FMT_YUYV;
        cam->cam_w      = w;
        cam->cam_h      = h;
    }

    struct v4l2_format v4l2fmt;
    v4l2fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam->fd, VIDIOC_G_FMT, &v4l2fmt);
    v4l2fmt.fmt.pix.pixelformat = cam->cam_pixfmt;
    v4l2fmt.fmt.pix.width       = cam->cam_w;
    v4l2fmt.fmt.pix.height      = cam->cam_h;
    if (ioctl(cam->fd, VIDIOC_S_FMT, &v4l2fmt) != -1) {
        ALOGD("VIDIOC_S_FMT      \n");
        ALOGD("------------------\n");
        ALOGD("width:        %d\n",    v4l2fmt.fmt.pix.width       );
        ALOGD("height:       %d\n",    v4l2fmt.fmt.pix.height      );
        ALOGD("pixfmt:       0x%0x\n", v4l2fmt.fmt.pix.pixelformat );
        ALOGD("field:        %d\n",    v4l2fmt.fmt.pix.field       );
        ALOGD("bytesperline: %d\n",    v4l2fmt.fmt.pix.bytesperline);
        ALOGD("sizeimage:    %d\n",    v4l2fmt.fmt.pix.sizeimage   );
        ALOGD("colorspace:   %d\n",    v4l2fmt.fmt.pix.colorspace  );
        cam->cam_stride = v4l2fmt.fmt.pix.bytesperline;
    }
    else {
        ALOGW("failed to camera preview size and pixel format !\n");
        close(cam->fd);
        cam->fd = 0;
        goto done;
    }

#if 0
    struct v4l2_streamparm streamparam;
    streamparam.parm.capture.timeperframe.numerator   = 1;
    streamparam.parm.capture.timeperframe.denominator = 30;
    streamparam.parm.capture.capturemode              = 0;
    streamparam.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(cam->fd, VIDIOC_S_PARM, &streamparam) != -1) {
        ioctl(cam->fd, VIDIOC_G_PARM, &streamparam);
        ALOGD("current camera frame rate: %d/%d !\n",
            streamparam.parm.capture.timeperframe.denominator,
            streamparam.parm.capture.timeperframe.numerator );
    }
    else {
        ALOGW("failed to set camera frame rate !\n");
    }
#endif

    struct v4l2_requestbuffers req;
    req.count  = VIDEO_CAPTURE_BUFFER_COUNT;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(cam->fd, VIDIOC_REQBUFS, &req);

    for (int i=0; i<VIDEO_CAPTURE_BUFFER_COUNT; i++)
    {
        cam->buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cam->buf.memory = V4L2_MEMORY_MMAP;
        cam->buf.index  = i;
        ioctl(cam->fd, VIDIOC_QUERYBUF, &cam->buf);

        cam->vbs[i].addr= mmap(NULL, cam->buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                               cam->fd, cam->buf.m.offset);
        cam->vbs[i].len = cam->buf.length;

        ioctl(cam->fd, VIDIOC_QBUF, &cam->buf);
    }

done:
    cam->thread_state = (1 << 1);
    pthread_create(&cam->thread_id, NULL, video_render_thread_proc, cam);

    return cam;
}

void camcdr_close(CAMCDR *cam)
{
    int i;

    if (!cam) return;

    // wait thread safely exited
    cam->thread_state |= (1 << 0);
    pthread_join(cam->thread_id, NULL);

    // unmap buffers
    for (i=0; i<VIDEO_CAPTURE_BUFFER_COUNT; i++) {
        munmap(cam->vbs[i].addr, cam->vbs[i].len);
    }

    // free sws context
    if (cam->swsctxt) {
        sws_freeContext(cam->swsctxt);
    }

    // close & free
    close(cam->fd);
    free(cam);
}

void camcdr_set_preview_window(CAMCDR *cam, const sp<ANativeWindow> win)
{
    if (cam) {
        cam->new_win     = win;
        cam->update_flag = 1;
    }
}

void camcdr_set_preview_target(CAMCDR *cam, const sp<IGraphicBufferProducer>& gbp)
{
    sp<ANativeWindow> win;
    if (gbp != 0) {
        // Using controlledByApp flag to ensure that the buffer queue remains in
        // async mode for the old camera API, where many applications depend
        // on that behavior.
        win = new Surface(gbp, /*controlledByApp*/ true);
    }
    camcdr_set_preview_window(cam, win);
}

void camcdr_start_preview(CAMCDR *cam)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (cam->fd > 0) {
        ioctl(cam->fd, VIDIOC_STREAMON, &type);
    }

    // start thread
    cam->thread_state &= ~(1 << 1);
}

void camcdr_stop_preview(CAMCDR *cam)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (cam->fd > 0) {
        ioctl(cam->fd, VIDIOC_STREAMOFF, &type);
    }

    // pause thread
    cam->thread_state |= ~(1 << 1);
}


