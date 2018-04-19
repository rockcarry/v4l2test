#ifndef __CAMDEV_H__
#define __CAMDEV_H__

// 包含头文件
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>
#include <ui/DisplayInfo.h>
#include <ui/GraphicBufferMapper.h>

using namespace android;

// 类型定义
// camdev capture callback
typedef int (*CAMDEV_CAPTURE_CALLBACK)(void *recorder, void *data[8], int linesize[8], int64_t pts);

enum {
    CAMDEV_PARAM_VIDEO_WIDTH,
    CAMDEV_PARAM_VIDEO_HEIGHT,
    CAMDEV_PARAM_VIDEO_PIXFMT,
    CAMDEV_PARAM_VIDEO_FRATE_NUM,
    CAMDEV_PARAM_VIDEO_FRATE_DEN,
};

// 函数定义
void* camdev_init (const char *dev, int sub, int w, int h, int frate);
void  camdev_close(void *ctxt);
void  camdev_reset(void *ctxt, int w, int h, int frate);

void  camdev_set_preview_window(void *ctxt, const sp<ANativeWindow> win);
void  camdev_set_preview_target(void *ctxt, const sp<IGraphicBufferProducer>& gbp);

void  camdev_capture_start(void *ctxt);
void  camdev_capture_stop (void *ctxt);
void  camdev_preview_start(void *ctxt);
void  camdev_preview_stop (void *ctxt);
void  camdev_set_watermark(void *ctxt, int x, int y, const char *wm);
void  camdev_set_callback (void *ctxt, void *callback, void *recorder);
void  camdev_set_param    (void *ctxt, int id, int value);
int   camdev_get_param    (void *ctxt, int id);

#endif









