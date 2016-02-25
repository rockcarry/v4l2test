
#define LOG_TAG "LibveDecoder"
#include <cutils/log.h>

#include "formatconvert.h"
#include "LibveDecoder.h"

static int GetStreamData(void* in, u8* buf0, u32 buf_size0, u8* buf1, u32 buf_size1, cedarv_stream_data_info_t* data_info)
{
    ALOGV("Starting get stream data!!");
    if (data_info->lengh <= buf_size0) {
        ALOGV("The stream lengh is %d, the buf_size0 is %d",data_info->lengh,buf_size0);
        memcpy(buf0, in, data_info->lengh);
    }
    else {
        if (data_info->lengh <= (buf_size0+buf_size1)) {
            ALOGV("The stream lengh is %d, the buf_size0 is %d,the buf_size1 is %d",data_info->lengh,buf_size0,buf_size1);
            memcpy(buf0, in, buf_size0);
            memcpy(buf1,((char*)in+buf_size0),(data_info->lengh-buf_size0));
        }
        else {
            return -1;
        }
    }

    data_info->flags |= CEDARV_FLAG_FIRST_PART;
    data_info->flags |= CEDARV_FLAG_LAST_PART;
    data_info->flags |= CEDARV_FLAG_PTS_VALID;

    return 0;
}

DecodeHandle* libveInit(int format) {
    int ret;
    DecodeHandle *dec_handle;

    dec_handle = malloc(sizeof(DecodeHandle));
    if (dec_handle == NULL) {
        ALOGW("malloc for DecodeHandle fail.\n");
        return NULL;
    }
    memset(dec_handle, 0, sizeof(DecodeHandle));

    if (decorder_mutex_init(&dec_handle->mDecoderMutex, CEDARV_DECODE) < 0) {
        ALOGE("ve mutex init fail!!");
        goto DEC_ERR1;
    }

    cedarx_hardware_init(0);
    decorder_mutex_lock(&dec_handle->mDecoderMutex);
    dec_handle->mDecoder = libcedarv_init(&ret);
    decorder_mutex_unlock(&dec_handle->mDecoderMutex);
    ALOGV("libcedarv_init!!");
    if (ret < 0){
        ALOGE("can not initialize the mDecoder library.\n");
        goto DEC_ERR2;
    }

    dec_handle->mStream_info.format             = format;
    dec_handle->mStream_info.sub_format         = CEDARV_SUB_FORMAT_UNKNOW;
    dec_handle->mStream_info.container_format   = CEDARV_CONTAINER_FORMAT_UNKNOW;
    dec_handle->mStream_info.video_width        = 1280;
    dec_handle->mStream_info.video_height       = 720;
    dec_handle->mStream_info.frame_rate         = 25 * 1000;
    dec_handle->mStream_info.frame_duration     = 0;
    dec_handle->mStream_info.init_data_len      = 0;
    dec_handle->mStream_info.init_data          = 0;

    (dec_handle->mDecoder)->set_vstream_info(dec_handle->mDecoder, &dec_handle->mStream_info); //* this mDecoder operation do not use hardware, so need not lock the mutex.

    ALOGD("mDecoder->open!!");
    decorder_mutex_lock(&dec_handle->mDecoderMutex);
    ret =(dec_handle->mDecoder)->open(dec_handle->mDecoder);
    decorder_mutex_unlock(&dec_handle->mDecoderMutex);
    if (ret < 0) {
        ALOGE("can not open mDecoder.\n");
        goto DEC_ERR3;
    }

    decorder_mutex_lock(&dec_handle->mDecoderMutex);
    (dec_handle->mDecoder)->ioctrl(dec_handle->mDecoder, CEDARV_COMMAND_PLAY, 0);
    decorder_mutex_unlock(&dec_handle->mDecoderMutex);
    ALOGD("libveInit OK !");
    return dec_handle;

DEC_ERR3:
    decorder_mutex_lock(&dec_handle->mDecoderMutex);
    libcedarv_exit(dec_handle->mDecoder);
    decorder_mutex_unlock(&dec_handle->mDecoderMutex);
DEC_ERR2:
    cedarx_hardware_exit(0);
    decorder_mutex_destroy(&dec_handle->mDecoderMutex);
DEC_ERR1:    
    free(dec_handle);
    ALOGD("libveInit FAIL !");
    return NULL;    
}

int libveExit(DecodeHandle *handle) {
    DecodeHandle *dec_handle = handle;

    if (dec_handle == NULL) {
        return -1;
    }

    decorder_mutex_lock(&dec_handle->mDecoderMutex);
    (dec_handle->mDecoder)->ioctrl(dec_handle->mDecoder, CEDARV_COMMAND_STOP, 0);
    decorder_mutex_unlock(&dec_handle->mDecoderMutex);
    
    decorder_mutex_lock(&dec_handle->mDecoderMutex);
    (dec_handle->mDecoder)->close(dec_handle->mDecoder);
    libcedarv_exit(dec_handle->mDecoder);
    decorder_mutex_unlock(&dec_handle->mDecoderMutex);
    cedarx_hardware_exit(0);

    decorder_mutex_destroy(&dec_handle->mDecoderMutex);
    free(dec_handle);
    ALOGD("libveExit OK !");

    return 0;
}

int libveDecode(DecodeHandle *handle, void *data_in, int data_size, int pts, int *decCount) {
    int     ret;
    u8*     buf0;
    u8*     buf1;
    u32     buf0size;
    u32     buf1size;
    DecodeHandle *dec_handle = handle;

    if (dec_handle == NULL) {
        return -1;
    }

    ALOGV("libveDecode !");
    ret = (dec_handle->mDecoder)->request_write(dec_handle->mDecoder, data_size, &buf0, &buf0size, &buf1, &buf1size);
    if (ret < 0) {
        ALOGE("request bit stream buffer fail.\n");
        return  ret;
    }

    ALOGV("GetStreamData!!");
    dec_handle->mData_info.lengh = data_size;
    dec_handle->mData_info.pts = pts;
    GetStreamData(data_in, buf0, buf0size, buf1, buf1size, &dec_handle->mData_info);

    (dec_handle->mDecoder)->update_data(dec_handle->mDecoder, &dec_handle->mData_info); //* this mDecoder operation do not use hardware, so need not lock the mutex.

    decorder_mutex_lock(&dec_handle->mDecoderMutex);
    ret = (dec_handle->mDecoder)->decode(dec_handle->mDecoder);
    decorder_mutex_unlock(&dec_handle->mDecoderMutex);

    if (ret == CEDARV_RESULT_ERR_NO_MEMORY || ret == CEDARV_RESULT_ERR_UNSUPPORTED) {
        ALOGE("bit stream is unsupported, ret = %d.\n", ret);
    } else if (ret==CEDARV_RESULT_OK) {
        ALOGE("Successfully return,decording!!");
    } else if (ret==CEDARV_RESULT_FRAME_DECODED) {
        ALOGV("One frame decorded!!");
    } else if (ret == CEDARV_RESULT_KEYFRAME_DECODED) {
        ALOGV("One key frame decorded!!");
    } else {
        ALOGE("decode fail. ret = %d !!", ret);
    }

    if (decCount) {
        (*decCount)++;
        ALOGV("FUNC:%s,LINE:%d,decCount=%d",__FUNCTION__,__LINE__,(*decCount));
    }

    return ret;
}

int libveGetFrame(DecodeHandle *handle, void *data_out, int width, int height, int *decCount) {
    int ret;
    cedarv_picture_t picture;
    DecodeHandle *dec_handle = handle;

    if (dec_handle == NULL) {
        return -1;
    }

    ret = (dec_handle->mDecoder)->display_request(dec_handle->mDecoder, &picture);
    ALOGV("The return of display_request is %d",ret);
    if (ret == 0){
        if (data_out != NULL) {
            DataFormatSoftwareConvert(&picture, data_out, width, height);
        }
        (dec_handle->mDecoder)->display_release(dec_handle->mDecoder, picture.id);
        ALOGV("mDecoder->display_release");
    }

    if (decCount) {
        (*decCount)--;
        if ((*decCount) < 0) {
            (*decCount) = 0;
        }
        ALOGV("FUNC:%s,LINE:%d,decCount=%d",__FUNCTION__,__LINE__,(*decCount));
    }
    return ret;
}


