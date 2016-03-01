#ifndef __LIBVE_DECORDER_H__
#define __LIBVE_DECORDER_H__

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>

#include <cedarx_hardware.h>
#include <CDX_Resource_Manager.h>

#include "libcedarv.h"
#include "libve_typedef.h"

#define VE_MUTEX_ENABLE  1

#if VE_MUTEX_ENABLE
#define decorder_mutex_lock(x)      ve_mutex_lock(x)
#define decorder_mutex_unlock(x)    ve_mutex_unlock(x)
#define decorder_mutex_init(x,y)    ve_mutex_init(x,y)
#define decorder_mutex_destroy(x)   ve_mutex_destroy(x)
#else
#define decorder_mutex_lock(x)
#define decorder_mutex_unlock(x)
#define decorder_mutex_init(x,y)    0
#define decorder_mutex_destroy(x)
#endif
 
#ifdef __cplusplus
extern "C" {
#endif

typedef struct DecodeHandle{
    ve_mutex_t mDecoderMutex;
    cedarv_decoder_t *mDecoder;
    cedarv_stream_info_t mStream_info;
    cedarv_stream_data_info_t mData_info;
}DecodeHandle;

DecodeHandle* libveInit(int format);
int libveExit(DecodeHandle *handle);
int libveDecode(DecodeHandle *handle, void *data_in, int data_size, int pts , int *decCount);
int libveGetFrame(DecodeHandle *handle, void *data_out, int width, int height, int *decCount);

#ifdef __cplusplus
}
#endif

#endif  /* __LIBVE_DECORDER_H__ */

