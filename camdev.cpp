#define LOG_TAG "camdev"

// 包含头文件
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <utils/Log.h>
#include "ffutils.h"
#include "ffjpeg.h"
#include "watermark.h"
#include "camdev.h"

extern "C" {
#include "libswscale/swscale.h"
}

#ifdef PLATFORM_ALLWINNER_A33
extern "C" int  ion_alloc_open (void);
extern "C" int  ion_alloc_close(void);
extern "C" void ion_flush_cache(void *pbuf, int size);
#endif

// 内部常量定义
#define DO_USE_VAR(v)   do { v = v; } while (0)
#define VIDEO_CAPTURE_BUFFER_COUNT  3
#define NATIVE_WIN_BUFFER_COUNT     3
#define DEF_WIN_PIX_FMT         HAL_PIXEL_FORMAT_YCrCb_420_SP // HAL_PIXEL_FORMAT_RGBX_8888 or HAL_PIXEL_FORMAT_YCrCb_420_SP
#define CAMDEV_GRALLOC_USAGE    (GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_WRITE_NEVER | GRALLOC_USAGE_HW_TEXTURE)

// 内部类型定义
struct video_buffer {
    void    *addr;
    unsigned len;
};

// camdev context
typedef struct {
    struct v4l2_buffer      buf;
    struct video_buffer     vbs[VIDEO_CAPTURE_BUFFER_COUNT];
    int                     fd;
    sp<ANativeWindow>       new_win;
    sp<ANativeWindow>       cur_win;
    int                     win_w;
    int                     win_h;
    #define CAMDEV_TS_EXIT       (1 << 0)  // exit threads
    #define CAMDEV_TS_PAUSE_C    (1 << 1)  // pause capture
    #define CAMDEV_TS_PAUSE_P    (1 << 2)  // pause preview
    #define CAMDEV_TS_PREVIEW    (1 << 3)  // enable preview
    sem_t                   sem_render;
    pthread_t               thread_id_render;
    pthread_t               thread_id_capture;
    int                     thread_state;
    int                     update_flag;
    int                     cam_pixfmt;
    int                     cam_stride;
    int                     cam_w;
    int                     cam_h;
    int                     cam_frate_num; // camdev frame rate num get from v4l2 interface
    int                     cam_frate_den; // camdev frame rate den get from v4l2 interface
    SwsContext             *swsctxt;
    CAMDEV_CAPTURE_CALLBACK callback;
    void                   *recorder;
    void                   *jpegdec;

    #define MAX_WATERMARK_SIZE  64
    int                     watermark_x;
    int                     watermark_y;
    char                    watermark_str[MAX_WATERMARK_SIZE];
} CAMDEV;

// 内部函数实现
static void render_v4l2(CAMDEV *cam,
                        void *dstbuf, int dstlen, int dstfmt, int dstw, int dsth,
                        void *srcbuf, int srclen, int srcfmt, int srcw, int srch)
{
    if (dstfmt != DEF_WIN_PIX_FMT) {
        return;
    }

    // src fmt
    AVPixelFormat sws_src_fmt = (AVPixelFormat)v4l2dev_pixfmt_to_ffmpeg_pixfmt(srcfmt);

    // dst fmt
    AVPixelFormat sws_dst_fmt = (AVPixelFormat)android_pixfmt_to_ffmpeg_pixfmt(dstfmt);

    // dst len
    if (dstlen == -1) {
        switch (dstfmt) {
        case HAL_PIXEL_FORMAT_RGB_565:      dstlen = dstw * dsth * 2; break;
        case HAL_PIXEL_FORMAT_RGBX_8888:    dstlen = dstw * dsth * 4; break;
        case HAL_PIXEL_FORMAT_YV12:         dstlen = dstw * dsth + dstw * dsth / 2; break;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP: dstlen = dstw * dsth + dstw * dsth / 2; break;
        default:                            dstlen = 0; break;
        }
    }

//  ALOGD("srcfmt = 0x%0x, srcw = %d, srch = %d, srclen = %d, sws_src_fmt = 0x%0x\n", srcfmt, srcw, srch, srclen, sws_src_fmt);
//  ALOGD("dstfmt = 0x%0x, dstw = %d, dsth = %d, dstlen = %d, sws_dst_fmt = 0x%0x\n", dstfmt, dstw, dsth, dstlen, sws_dst_fmt);

    // memcpy if same fmt and size
    if (sws_src_fmt == sws_dst_fmt && srcw == dstw && srch == dsth) {
//      ALOGD("===ck=== sws_src_fmt = 0x%0x, sws_dst_fmt = 0x%0x", sws_src_fmt, sws_dst_fmt);
//      memcpy(dstbuf, srcbuf, dstlen < srclen ? dstlen : srclen);
        memcpy(dstbuf, srcbuf, srcw * srch);
        memcpy((uint8_t*)dstbuf + srcw * srch, (uint8_t*)srcbuf + srcw * srch + 1, srcw * srch / 2 - 1);
        return;
    }

    uint8_t* dst_data[AV_NUM_DATA_POINTERS]     = { (uint8_t*)dstbuf, (uint8_t*)dstbuf + dstw * dsth, (uint8_t*)dstbuf + dstw * dsth };
    uint8_t* src_data[AV_NUM_DATA_POINTERS]     = { (uint8_t*)srcbuf, (uint8_t*)srcbuf + srcw * srch, (uint8_t*)srcbuf + srcw * srch };
    int      dst_linesize[AV_NUM_DATA_POINTERS] = { dstw, dstw / 1, dstw / 1 };
    int      src_linesize[AV_NUM_DATA_POINTERS] = { srcw, srcw / 1, srcw / 1 };
    if (srcfmt == V4L2_PIX_FMT_YUYV) {
        src_linesize[0] = srcw * 2;
    }
    if (srcfmt == V4L2_PIX_FMT_MJPEG) {
        AVFrame *pic = ffjpeg_decoder_decode(cam->jpegdec, srcbuf, srclen);
        sws_src_fmt = (AVPixelFormat)pic->format;
        memcpy(src_data    , pic->data    , sizeof(src_data    ));
        memcpy(src_linesize, pic->linesize, sizeof(src_linesize));
    }
    if (dstfmt == HAL_PIXEL_FORMAT_RGBX_8888) {
        dst_linesize[0] = dstw * 4;
    }

    //++ do sws scale
    if (cam->win_w != dstw || cam->win_h != dsth) {
        if (cam->swsctxt) {
            sws_freeContext(cam->swsctxt);
        }
        cam->swsctxt = sws_getContext(srcw, srch, sws_src_fmt, dstw, dsth, sws_dst_fmt, SWS_POINT, 0, 0, 0);
        cam->win_w   = dstw;
        cam->win_h   = dsth;
    }
    sws_scale(cam->swsctxt, src_data, src_linesize, 0, srch, dst_data, dst_linesize);
    //-- do sws scale
}

static void* camdev_capture_thread_proc(void *param)
{
    CAMDEV  *cam = (CAMDEV*)param;
    int      err;

    //++ for select
    fd_set        fds;
    struct timeval tv;
    //-- for select

    while (!(cam->thread_state & CAMDEV_TS_EXIT)) {
        FD_ZERO(&fds);
        FD_SET (cam->fd, &fds);
        tv.tv_sec  = 1;
        tv.tv_usec = 0;
        if (select(cam->fd + 1, &fds, NULL, NULL, &tv) <= 0) {
            ALOGD("select error or timeout !\n");
            continue;
        }

        if (cam->thread_state & CAMDEV_TS_PAUSE_C) {
            cam->thread_state |= (CAMDEV_TS_PAUSE_C << 16);
            usleep(10*1000);
            continue;
        }

        // dequeue camera video buffer
        if (-1 == ioctl(cam->fd, VIDIOC_DQBUF, &cam->buf)) {
            ALOGD("failed to de-queue buffer !\n");
            continue;
        }

        if (cam->watermark_str[0]) {
            watermark_putstring(cam->vbs[cam->buf.index].addr, cam->cam_w, cam->cam_h, cam->watermark_x, cam->watermark_y, cam->watermark_str);
#ifdef PLATFORM_ALLWINNER_A33
            ion_flush_cache(cam->vbs[cam->buf.index].addr, cam->buf.bytesused);
#endif
        }

//      ALOGD("%d. bytesused: %d, sequence: %d, length = %d\n", cam->buf.index, cam->buf.bytesused,
//              cam->buf.sequence, cam->buf.length);
//      ALOGD("timestamp: %ld, %ld\n", cam->buf.timestamp.tv_sec, cam->buf.timestamp.tv_usec);

        if (cam->thread_state & CAMDEV_TS_PREVIEW) {
            sem_post(&cam->sem_render);
        }

        if (cam->callback) {
            int      camw = cam->cam_w;
            int      camh = cam->cam_h;
            uint8_t *cbuf = (uint8_t*)cam->vbs[cam->buf.index].addr;
            void    *data[AV_NUM_DATA_POINTERS]     = { (uint8_t*)cbuf, (uint8_t*)cbuf + camw * camh, (uint8_t*)cbuf + camw * camh };
            int      linesize[AV_NUM_DATA_POINTERS] = { camw, camw / 1, camw / 1 };
            int64_t  pts  = cam->buf.timestamp.tv_sec * 1000000 + cam->buf.timestamp.tv_usec;
            if (cam->cam_pixfmt == V4L2_PIX_FMT_YUYV) {
                linesize[0] = camw * 2;
            }
            linesize[AV_NUM_DATA_POINTERS-1] = cam->buf.bytesused;   // store the buffer size
            data    [AV_NUM_DATA_POINTERS-2] = (uint8_t*)cam->buf.m.offset;  // store the physic addr1
            data    [AV_NUM_DATA_POINTERS-1] = (uint8_t*)cam->buf.m.offset + camw * camh;  // store the physic addr2
            cam->callback(cam->recorder, data, linesize, pts);
        }

        // requeue camera video buffer
        if (-1 == ioctl(cam->fd, VIDIOC_QBUF , &cam->buf)) {
            ALOGD("failed to en-queue buffer !\n");
        }
    }

//  ALOGD("camdev_capture_thread_proc exited !");
    return NULL;
}

static void* camdev_render_thread_proc(void *param)
{
    CAMDEV  *cam = (CAMDEV*)param;
    int      err;

    while (!(cam->thread_state & CAMDEV_TS_EXIT)) {
        if (0 != sem_wait(&cam->sem_render)) {
            ALOGD("failed to wait sem_render !\n");
            break;
        }

        if (cam->thread_state & CAMDEV_TS_PAUSE_P) {
            cam->thread_state |= (CAMDEV_TS_PAUSE_P << 16);
            usleep(10*1000);
            continue;
        }

        if (cam->update_flag) {
            cam->cur_win = cam->new_win;
            if (cam->cur_win != NULL) {
                native_window_set_usage             (cam->cur_win.get(), CAMDEV_GRALLOC_USAGE);
                native_window_set_scaling_mode      (cam->cur_win.get(), NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
                native_window_set_buffer_count      (cam->cur_win.get(), NATIVE_WIN_BUFFER_COUNT);
                native_window_set_buffers_format    (cam->cur_win.get(), DEF_WIN_PIX_FMT);
                native_window_set_buffers_dimensions(cam->cur_win.get(), cam->cam_w, cam->cam_h);
            }
            cam->update_flag = 0;
        }

        char *data = (char*)cam->vbs[cam->buf.index].addr;
        int   len  = cam->buf.bytesused;

        ANativeWindowBuffer *buf;
        if (cam->cur_win != NULL && 0 == native_window_dequeue_buffer_and_wait(cam->cur_win.get(), &buf)) {
            GraphicBufferMapper &mapper = GraphicBufferMapper::get();
            Rect bounds(buf->width, buf->height);
            void *dst = NULL;

            if (0 == mapper.lock(buf->handle, GRALLOC_USAGE_SW_WRITE_OFTEN, bounds, &dst)) {
                render_v4l2(cam,
                    dst , -1 , buf->format    , buf->width, buf->height,
                    data, len, cam->cam_pixfmt, cam->cam_w, cam->cam_h);
                mapper.unlock(buf->handle);
            }

            if ((err = cam->cur_win->queueBuffer(cam->cur_win.get(), buf, -1)) != 0) {
                ALOGW("Surface::queueBuffer returned error %d\n", err);
            }
        }
    }

//  ALOGD("camdev_render_thread_proc exited !");
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

#if 0
    ALOGD("\n");
    ALOGD("VIDIOC_ENUM_FMT   \n");
    ALOGD("------------------\n");
#endif

    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) != -1) {
#if 0
        ALOGD("%d. flags: %d, description: %-16s, pixelfmt: 0x%0x\n",
            fmtdesc.index, fmtdesc.flags,
            fmtdesc.description,
            fmtdesc.pixelformat);
#endif
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
    ret = ioctl(fd, VIDIOC_TRY_FMT, &v4l2fmt);
    if (ret < 0)
    {
        ALOGE("VIDIOC_TRY_FMT Failed: %s\n", strerror(errno));
        return ret;
    }

    // driver surpport this size
    *width  = v4l2fmt.fmt.pix.width;
    *height = v4l2fmt.fmt.pix.height;

    ALOGD("using pixel format: 0x%0x\n", fmt);
    ALOGD("using frame size: %d, %d\n\n", *width, *height);
    return 0;
}

// 函数实现
void* camdev_init(const char *dev, int sub, int w, int h, int frate)
{
    CAMDEV *cam = new CAMDEV();
    if (!cam) {
        return NULL;
    } else {
        memset(cam, 0, sizeof(CAMDEV));
    }

#ifdef PLATFORM_ALLWINNER_A33
    ion_alloc_open();
#endif

    // open camera device
    cam->fd = open(dev, O_RDWR | O_NONBLOCK);
    if (cam->fd < 0) {
        ALOGW("failed to open video device: %s\n", dev);
        delete cam;
        return NULL;
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

    if (0 == v4l2_try_fmt_size(cam->fd, V4L2_PIX_FMT_MJPEG, &w, &h)) {
        cam->cam_pixfmt = V4L2_PIX_FMT_MJPEG;
        cam->cam_w      = w;
        cam->cam_h      = h;
    }
    else if (0 == v4l2_try_fmt_size(cam->fd, V4L2_PIX_FMT_NV12, &w, &h)) {
//      ALOGD("===ck=== V4L2_PIX_FMT_NV12");
        cam->cam_pixfmt = V4L2_PIX_FMT_NV12;
        cam->cam_w      = w;
        cam->cam_h      = h;
    }
    else if (0 == v4l2_try_fmt_size(cam->fd, V4L2_PIX_FMT_NV21, &w, &h)) {
//      ALOGD("===ck=== V4L2_PIX_FMT_NV21");
        cam->cam_pixfmt = V4L2_PIX_FMT_NV21;
        cam->cam_w      = w;
        cam->cam_h      = h;
    }
    else if (0 == v4l2_try_fmt_size(cam->fd, V4L2_PIX_FMT_YUYV, &w, &h)) {
//      ALOGD("===ck=== V4L2_PIX_FMT_YUYV");
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
        ALOGW("failed to set camera preview size and pixel format !\n");
        close(cam->fd);
        delete cam;
        return NULL;
    }

    struct v4l2_streamparm streamparam;
    streamparam.parm.capture.timeperframe.numerator   = 1;
    streamparam.parm.capture.timeperframe.denominator = frate;
    streamparam.parm.capture.capturemode              = 0;
    streamparam.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(cam->fd, VIDIOC_S_PARM, &streamparam) != -1) {
        ioctl(cam->fd, VIDIOC_G_PARM, &streamparam);
        ALOGD("current camera frame rate: %d/%d !\n",
            streamparam.parm.capture.timeperframe.denominator,
            streamparam.parm.capture.timeperframe.numerator );
        cam->cam_frate_num = streamparam.parm.capture.timeperframe.denominator;
        cam->cam_frate_den = streamparam.parm.capture.timeperframe.numerator;
    }
    else {
        ALOGW("failed to set camera frame rate !\n");
    }

    struct v4l2_requestbuffers req;
    req.count  = VIDEO_CAPTURE_BUFFER_COUNT;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(cam->fd, VIDIOC_REQBUFS, &req);

    for (int i=0; i<VIDEO_CAPTURE_BUFFER_COUNT; i++) {
        cam->buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cam->buf.memory = V4L2_MEMORY_MMAP;
        cam->buf.index  = i;
        ioctl(cam->fd, VIDIOC_QUERYBUF, &cam->buf);

        cam->vbs[i].addr= mmap(NULL, cam->buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                               cam->fd, cam->buf.m.offset);
        cam->vbs[i].len = cam->buf.length;

        ioctl(cam->fd, VIDIOC_QBUF, &cam->buf);
    }

    if (cam->cam_pixfmt == V4L2_PIX_FMT_MJPEG) {
        cam->jpegdec = ffjpeg_decoder_init();
    }

    // set capture & preview pause flags
    cam->thread_state |= (CAMDEV_TS_PAUSE_C | CAMDEV_TS_PAUSE_P);

    // create sem_render
    sem_init(&cam->sem_render, 0, 0);

    // create capture thread
    pthread_create(&cam->thread_id_capture, NULL, camdev_capture_thread_proc, cam);

    // create render thread
    pthread_create(&cam->thread_id_render , NULL, camdev_render_thread_proc , cam);

    return cam;
}

void camdev_close(void *ctxt)
{
    CAMDEV *cam = (CAMDEV*)ctxt;
    if (!cam) return;

    // wait thread safely exited
    cam->thread_state |= CAMDEV_TS_EXIT; sem_post(&cam->sem_render);
    pthread_join(cam->thread_id_capture, NULL);
    pthread_join(cam->thread_id_render , NULL);

    if (cam->cam_pixfmt == V4L2_PIX_FMT_MJPEG) {
        ffjpeg_decoder_free(cam->jpegdec);
    }

    // unmap buffers
    for (int i=0; i<VIDEO_CAPTURE_BUFFER_COUNT; i++) {
        munmap(cam->vbs[i].addr, cam->vbs[i].len);
    }

    // free sws context
    if (cam->swsctxt) {
        sws_freeContext(cam->swsctxt);
    }

    // close & free
    close(cam->fd);
    delete cam;

#ifdef PLATFORM_ALLWINNER_A33
    ion_alloc_close();
#endif
}

void camdev_reset(void *ctxt, int w, int h, int frate)
{
    CAMDEV *cam = (CAMDEV*)ctxt;
    int old_thread_state = cam->thread_state;

    // stop preview thread
    cam->thread_state &=~(CAMDEV_TS_PAUSE_P << 16);
    cam->thread_state |= (CAMDEV_TS_PAUSE_P << 0 );
    sem_post(&cam->sem_render);
    while ((cam->thread_state & (CAMDEV_TS_PAUSE_P << 16)) == 0) usleep(10000);

    // stop capture thread
    camdev_capture_stop(cam);

    // unmap buffers
    for (int i=0; i<VIDEO_CAPTURE_BUFFER_COUNT; i++) {
        munmap(cam->vbs[i].addr, cam->vbs[i].len);
    }

    cam->cam_w = w;
    cam->cam_h = h;
    v4l2_try_fmt_size(cam->fd, cam->cam_pixfmt, &cam->cam_w, &cam->cam_h);

    struct v4l2_format v4l2fmt;
    v4l2fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam->fd, VIDIOC_G_FMT, &v4l2fmt);
    v4l2fmt.fmt.pix.pixelformat = cam->cam_pixfmt;
    v4l2fmt.fmt.pix.width       = cam->cam_w;
    v4l2fmt.fmt.pix.height      = cam->cam_h;
    ioctl(cam->fd, VIDIOC_S_FMT, &v4l2fmt);

    struct v4l2_streamparm streamparam;
    streamparam.parm.capture.timeperframe.numerator   = 1;
    streamparam.parm.capture.timeperframe.denominator = frate;
    streamparam.parm.capture.capturemode              = 0;
    streamparam.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam->fd, VIDIOC_S_PARM, &streamparam);
    ioctl(cam->fd, VIDIOC_G_PARM, &streamparam);
    cam->cam_frate_num = streamparam.parm.capture.timeperframe.denominator;
    cam->cam_frate_den = streamparam.parm.capture.timeperframe.numerator;

    // remap buffers
    struct v4l2_requestbuffers req;
    req.count  = VIDEO_CAPTURE_BUFFER_COUNT;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(cam->fd, VIDIOC_REQBUFS, &req);
    for (int i=0; i<VIDEO_CAPTURE_BUFFER_COUNT; i++) {
        cam->buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cam->buf.memory = V4L2_MEMORY_MMAP;
        cam->buf.index  = i;
        ioctl(cam->fd, VIDIOC_QUERYBUF, &cam->buf);
        cam->vbs[i].addr= mmap(NULL, cam->buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                               cam->fd, cam->buf.m.offset);
        cam->vbs[i].len = cam->buf.length;
        ioctl(cam->fd, VIDIOC_QBUF, &cam->buf);
    }

    // set update_flag
    cam->update_flag = 1;

    // start capture if needed
    if (!(old_thread_state & CAMDEV_TS_PAUSE_C)) {
        camdev_capture_start(ctxt);
    }

    // clear preview pause flags
    if (!(old_thread_state & CAMDEV_TS_PAUSE_P)) {
        cam->thread_state &= ~CAMDEV_TS_PAUSE_P;
    }
}

void camdev_set_preview_window(void *ctxt, const sp<ANativeWindow> win)
{
    CAMDEV *cam = (CAMDEV*)ctxt;
    if (!cam) return;
    cam->new_win     = win;
    cam->update_flag = 1;
}

void camdev_set_preview_target(void *ctxt, const sp<IGraphicBufferProducer>& gbp)
{
    CAMDEV *cam = (CAMDEV*)ctxt;
    if (!cam) return;
    sp<ANativeWindow> win;
    if (gbp != 0) {
        // Using controlledByApp flag to ensure that the buffer queue remains in
        // async mode for the old camera API, where many applications depend
        // on that behavior.
        win = new Surface(gbp, /*controlledByApp*/ true);
    }
    camdev_set_preview_window(cam, win);
}

void camdev_capture_start(void *ctxt)
{
    CAMDEV *cam = (CAMDEV*)ctxt;
    // check fd valid
    if (!cam || cam->fd <= 0) return;

    // turn on stream
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam->fd, VIDIOC_STREAMON, &type);

    // resume thread
    cam->thread_state &= ~CAMDEV_TS_PAUSE_C;
}

void camdev_capture_stop(void *ctxt)
{
    CAMDEV *cam = (CAMDEV*)ctxt;
    // check fd valid
    if (!cam || cam->fd <= 0) return;

    // pause thread
    cam->thread_state |= CAMDEV_TS_PAUSE_C;

    // turn off stream
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam->fd, VIDIOC_STREAMOFF, &type);
}

void camdev_preview_start(void *ctxt)
{
    CAMDEV *cam = (CAMDEV*)ctxt;
    if (!cam) return;
    // set start prevew flag
    cam->thread_state &=~CAMDEV_TS_PAUSE_P;
    cam->thread_state |= CAMDEV_TS_PREVIEW;
}

void camdev_preview_stop(void *ctxt)
{
    CAMDEV *cam = (CAMDEV*)ctxt;
    if (!cam) return;
    // set stop prevew flag
    cam->thread_state |= CAMDEV_TS_PAUSE_P;
    cam->thread_state &=~CAMDEV_TS_PREVIEW;
}

void camdev_set_watermark(void *ctxt, int x, int y, const char *wm)
{
    CAMDEV *cam = (CAMDEV*)ctxt;
    if (!cam) return;
    cam->watermark_x = x;
    cam->watermark_y = y;
    strncpy(cam->watermark_str, wm, MAX_WATERMARK_SIZE - 1);
    cam->watermark_str[MAX_WATERMARK_SIZE - 1] = '\0';
}

void camdev_set_callback(void *ctxt, void *callback, void *recorder)
{
    CAMDEV *cam = (CAMDEV*)ctxt;
    if (!cam) return;
    cam->callback = (CAMDEV_CAPTURE_CALLBACK)callback;
    cam->recorder = recorder;
}

void camdev_set_param(void *ctxt, int id, int value)
{
    // todo...
}

int camdev_get_param(void *ctxt, int id)
{
    CAMDEV *cam = (CAMDEV*)ctxt;
    if (!cam) return 0;
    switch (id) {
    case CAMDEV_PARAM_VIDEO_WIDTH:
        return cam->cam_w;
    case CAMDEV_PARAM_VIDEO_HEIGHT:
        return cam->cam_h;
    case CAMDEV_PARAM_VIDEO_PIXFMT:
        return cam->cam_pixfmt;
    case CAMDEV_PARAM_VIDEO_FRATE_NUM:
        return cam->cam_frate_num;
    case CAMDEV_PARAM_VIDEO_FRATE_DEN:
        return cam->cam_frate_den;
    }
    return 0;
}



