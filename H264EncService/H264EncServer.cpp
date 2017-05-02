#include "cedarxh264enc.h"
#include "H264EncService.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

namespace android
{

IMPLEMENT_META_INTERFACE(H264EncService, "android.mediaserver.IH264EncService");

// 内部类型定义
// h264hwenc context
typedef struct {
    VENC_DEVICE *encdev;
    int          iw;
    int          ih;
    int          ow;
    int          oh;
    uint8_t      sps_pps_buf[32];
    int          sps_pps_len;
    int          firstframe;
    ve_mutex_t   mutex;
} H264ENC;

// 函数实现
static void *h264hwenc_cedarx_init(int iw, int ih, int ow, int oh, int frate, int bitrate)
{
    int level = 0;
    int ret   = 0;
    H264ENC *enc = (H264ENC*)calloc(1, sizeof(H264ENC));
    if (!enc) {
        ALOGE("failed to allocate h264hwenc context !\n");
        return NULL;
    }

    if (ve_mutex_init(&enc->mutex, CEDARV_ENCODE) < 0) {
        ALOGE("ve_mutex_init fail!!");
        goto failed;
    }

    // cedarx hardware init
    cedarx_hardware_init(0);

    ve_mutex_lock  (&enc->mutex);
    enc->encdev = H264EncInit(&ret);
    ve_mutex_unlock(&enc->mutex);
    if (ret < 0) {
        ALOGD("H264EncInit failed !");
        goto failed;
    }

    if (ow >= 1080) { // 1080p
        level = 41;
    } else if (ow >= 720) { // 720p
        level = 31;
    } else { // 640p
        level = 30;
    }

    __video_encode_format_t enc_fmt;
    memset(&enc_fmt, 0, sizeof(__video_encode_format_t));
    enc_fmt.src_width       = iw;
    enc_fmt.src_height      = ih;
    enc_fmt.width           = ow;
    enc_fmt.height          = oh;
    enc_fmt.frame_rate      = frate * 1000;
    enc_fmt.color_format    = PIXEL_YUV420;
    enc_fmt.color_space     = BT601;
    enc_fmt.qp_max          = 40;
    enc_fmt.qp_min          = 20;
    enc_fmt.avg_bit_rate    = bitrate;
    enc_fmt.maxKeyInterval  = frate;
    enc_fmt.profileIdc      = 66; /* baseline profile */
    enc_fmt.levelIdc        = level;

    ve_mutex_lock  (&enc->mutex);
    enc->encdev->IoCtrl(enc->encdev, VENC_SET_ENC_INFO_CMD, (unsigned int)(&enc_fmt));
    ret = enc->encdev->open(enc->encdev);
    ve_mutex_unlock(&enc->mutex);
    if (ret < 0) {
        ALOGD("open H264Enc failed !");
        goto failed;
    }

    __data_container sps_pps_info;
    memset(&sps_pps_info, 0, sizeof(sps_pps_info));
    enc->encdev->IoCtrl(enc->encdev, VENC_GET_SPS_PPS_DATA, (unsigned int)(&sps_pps_info));
    enc->sps_pps_len = min(sps_pps_info.length, (int)sizeof(enc->sps_pps_buf));
    memcpy(enc->sps_pps_buf, sps_pps_info.data, enc->sps_pps_len);
    /*
    ALOGD("sps_pps length: %d, data: %0x %0x %0x %0x %0x %0x %0x %0x", enc->sps_pps_len,
        enc->sps_pps_buf[0], enc->sps_pps_buf[1], enc->sps_pps_buf[2], enc->sps_pps_buf[3],
        enc->sps_pps_buf[4], enc->sps_pps_buf[5], enc->sps_pps_buf[6], enc->sps_pps_buf[7]);
    */

    enc->firstframe = 1;
    return enc;

failed:
    if (enc->encdev) {
        H264EncExit(enc->encdev);
    }
    if (enc->mutex) {
        ve_mutex_destroy(&enc->mutex);
    }
    free(enc);
    return NULL;
}

static void h264hwenc_cedarx_close(void *ctxt)
{
    H264ENC *enc = (H264ENC*)ctxt;
    if (!enc) return;

    ve_mutex_lock  (&enc->mutex);
    if (enc->encdev) {
        enc->encdev->close(enc->encdev);
        H264EncExit(enc->encdev);
    }
    ve_mutex_unlock(&enc->mutex);

    // cedarx hardware exit
    cedarx_hardware_exit(0);

    free(enc);
}

static int h264hwenc_cedarx_encode(void *ctxt, int64_t pts, void *phyy, void *phyc, int timeout, Parcel *reply)
{
    H264ENC *enc = (H264ENC*)ctxt;
    if (!enc) return -1;

    __venc_frmbuf_info fbufinfo;
    memset(&fbufinfo, 0, sizeof(fbufinfo));
    fbufinfo.addrY      = (uint8_t*)phyy;
    fbufinfo.addrCb     = (uint8_t*)phyc;
    fbufinfo.color_fmt  = PIXEL_YUV420;
    fbufinfo.color_space= BT601;
    fbufinfo.pts_valid  = 1;
    fbufinfo.pts        = pts;
    ve_mutex_lock  (&enc->mutex);
    int ret = enc->encdev->encode(enc->encdev, &fbufinfo);
    ve_mutex_unlock(&enc->mutex);
    if (ret < 0) {
        ALOGD("cedarx h264 encode frame failed !");
        usleep(10000);
        return -1;
    }

#if 0
    //++ wait until encoder has data output
    do {
        ret = enc->encdev->hasOutputStream(enc->encdev);
        ALOGD("hasOutputStream ret = %d.", ret);
        if (!ret) { usleep(10000); timeout -= 10; }
    } while (!ret && timeout > 0);
    if (!ret) {
        ALOGD("wait for output stream timeout !");
        return -1;
    }
    //-- wait until encoder has data output
#endif

    // get bit stream
    __vbv_data_ctrl_info_t datainfo;
    ret = enc->encdev->GetBitStreamInfo(enc->encdev, &datainfo);
    if (ret != 0) {
        ALOGD("GetBitStreamInfo failed !");
        return -1;
    }

    // make sure first frame is key frame
    if (enc->firstframe) { enc->firstframe = 0; datainfo.keyFrameFlag = 1; }

    //++ reallocate buffer if needed
    int len = datainfo.uSize0 + datainfo.uSize1 + (datainfo.keyFrameFlag ? enc->sps_pps_len : 0);
    //-- reallocate buffer if needed

    // write key frame flag
    reply->writeInt32(datainfo.keyFrameFlag);

    // write data length
    reply->writeInt32(len);

    // write data buffer
    uint8_t *buf = (uint8_t*)reply->writeInplace(len);

    int offset = 0;
    if (datainfo.keyFrameFlag) {
        memcpy(buf + offset, enc->sps_pps_buf, enc->sps_pps_len); offset += enc->sps_pps_len;
    }
    
    memcpy(buf + offset, datainfo.pData0, datainfo.uSize0); offset += datainfo.uSize0;
    memcpy(buf + offset, datainfo.pData1, datainfo.uSize1); offset += datainfo.uSize1;

    // release bit stream
    enc->encdev->ReleaseBitStreamInfo(enc->encdev, datainfo.idx);

    return len;
}

status_t BnH264EncService::onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags) {
    switch (code) {
        case INIT: {
            int32_t iw = data.readInt32();
            int32_t ih = data.readInt32();
            int32_t ow = data.readInt32();
            int32_t oh = data.readInt32();
            int32_t fr = data.readInt32();
            int32_t br = data.readInt32();
            int32_t handle = (int)h264hwenc_cedarx_init(iw, ih, ow, oh, fr, br);
            reply->writeInt32(handle);
        }
        break;
    case CLOSE: {
            void *handle = (void*)data.readInt32();
            h264hwenc_cedarx_close(handle);
        }
        break;
    case ENCODE: {
            void   *handle = (void*)data.readInt32();
            int64_t pts    = data.readInt64();
            void   *phyy   = (void*)data.readInt32();
            void   *phyc   = (void*)data.readInt32();
            int     timeout= data.readInt32();
            h264hwenc_cedarx_encode(handle, pts, phyy, phyc, timeout, reply);
        }
        break;
    default: return BBinder::onTransact(code, data, reply, flags);
    }
    return NO_ERROR;
}

int  BnH264EncService::init  (int iw, int ih, int ow, int oh, int frate, int bitrate) { return 0; }
void BnH264EncService::close (int handle) {}
int  BnH264EncService::encode(int handle, int64_t pts, void *phyy, void *phyc, int timeout, void *param) { return 0; }

}

#if 0
int main()
{
    sp <ProcessState> proc(ProcessState::self());
    sp <IServiceManager> sm = defaultServiceManager();
    sm->addService(String16("mediaserver.h264enc"), new BnH264EncService());

    ProcessState::self()->startThreadPool();
    IPCThreadState::self()->joinThreadPool();
    return 0;
}
#endif

