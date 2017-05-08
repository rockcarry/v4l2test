#define LOG_TAG "h264hwenc"

// 包含头文件
#include <jni.h>
#include <stdlib.h>
#include <pthread.h>
#include <utils/Log.h>
#include "ffencoder.h"
#include "h264hwenc.h"

#include <binder/Binder.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>

namespace android
{
    class IH264EncService : public IInterface {
    public:
        DECLARE_META_INTERFACE(H264EncService);
        virtual int  init(int iw, int ih, int ow, int oh, int frate, int bitrate) = 0;
        virtual void close(int handle) = 0;
        virtual int  encode(int handle, int64_t pts, void *phyy, void *phyc, int timeout, void *param) = 0;
    };

    enum {
        INIT = IBinder::FIRST_CALL_TRANSACTION,
        CLOSE,
        ENCODE,
    };

    // client class
    class BpH264EncService: public BpInterface<IH264EncService> {
    public:
        BpH264EncService(const sp<IBinder>& impl) : BpInterface<IH264EncService>(impl) {}

        int init(int iw, int ih, int ow, int oh, int frate, int bitrate) {
            Parcel data, reply;
            data.writeInt32(iw);
            data.writeInt32(ih);
            data.writeInt32(ow);
            data.writeInt32(oh);
            data.writeInt32(frate);
            data.writeInt32(bitrate);
            remote()->transact(INIT, data, &reply);
            return reply.readInt32();
        }

        void close(int handle) {
            Parcel data, reply;
            data.writeInt32(handle);
            remote()->transact(CLOSE, data, &reply);
        }

        int encode(int handle, int64_t pts, void *phyy, void *phyc, int timeout, void *param) {
            Parcel   data, reply;
            int      key, len;
            uint8_t *buf;

            data.writeInt32(handle);
            data.writeInt64(pts);
            data.writeInt32((int32_t)phyy);
            data.writeInt32((int32_t)phyc);
            data.writeInt32(timeout);
            remote()->transact(ENCODE, data, &reply);

            key = reply.readInt32();
            len = reply.readInt32();
            buf = (uint8_t*)reply.readInplace(len);

            AVPacket pkt;
            memset(&pkt, 0, sizeof(pkt));
            pkt.flags |= key ? AV_PKT_FLAG_KEY : 0;
            pkt.data   = buf;
            pkt.size   = len;
            pkt.pts    = pts;
            pkt.dts    = pts;
            ffencoder_write_video_frame(param, &pkt);
            return len;
        }
    };

    IMPLEMENT_META_INTERFACE(H264EncService, "android.mediaserver.IH264EncService");
}

// 内部类型定义
// h264hwenc context
typedef struct {
    void                     *ffencoder;
    android::IH264EncService *cs;
    int                       handle;
} H264ENC;

// 函数实现
void *h264hwenc_mediaserver_init(int iw, int ih, int ow, int oh, int frate, int bitrate, void *ffencoder)
{
    H264ENC *enc = (H264ENC*)calloc(1, sizeof(H264ENC));
    if (!enc) {
        ALOGE("failed to allocate h264hwenc context !\n");
        return NULL;
    }

    android::sp<android::IServiceManager> sm     = android::defaultServiceManager();
    android::sp<android::IBinder        > binder = sm->getService(android::String16("media.h264enc"));
    android::sp<android::IH264EncService> cs     = android::interface_cast<android::IH264EncService>(binder);

    enc->ffencoder = ffencoder;
    enc->cs        = cs.get();
    enc->handle    = cs->init(iw, ih, ow, oh, frate, bitrate);
    return enc;
}

void h264hwenc_mediaserver_close(void *ctxt)
{
    H264ENC *enc = (H264ENC*)ctxt;
    if (!enc) return;

    enc->cs->close(enc->handle);
    enc->cs = NULL;
    free(enc);
}

int h264hwenc_mediaserver_picture_format(void *ctxt)
{
    return AV_PIX_FMT_NV12;
}

int h264hwenc_mediaserver_picture_alloc(void *ctxt, AVFrame *frame)
{
    frame->format = AV_PIX_FMT_NONE;
    return 0;
}

int h264hwenc_mediaserver_picture_free(void *ctxt, AVFrame *frame)
{
    // do nothing
    return 0;
}

int h264hwenc_mediaserver_encode(void *ctxt, AVFrame *frame, int timeout)
{
    H264ENC *enc = (H264ENC*)ctxt;
    if (!enc) return -1;

    enc->cs->encode(enc->handle, frame->pts, frame->data[4], frame->data[5], timeout, enc->ffencoder);
    return 0;
}




