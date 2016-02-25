#ifndef __FORMAT_CONVERT_H__
#define __FORMAT_CONVERT_H__

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <pthread.h>

#include "libcedarv.h"
 
#ifdef __cplusplus
extern "C" {
#endif

enum FORMAT_CONVERT_COLORFORMAT {
    CONVERT_COLOR_FORMAT_NONE = 0,
    CONVERT_COLOR_FORMAT_YUV420PLANNER,
    CONVERT_COLOR_FORMAT_YUV422PLANNER,
    CONVERT_COLOR_FORMAT_YUV420MB,
    CONVERT_COLOR_FORMAT_YUV422MB,
};

typedef struct ScalerParameter {
    int mode; //0: YV12 1:thumb yuv420p
    int format_in;
    int format_out;

    int width_in;
    int height_in;

    int width_out;
    int height_out;

    void *addr_y_in;
    void *addr_c_in;
    unsigned int addr_y_out;
    unsigned int addr_u_out;
    unsigned int addr_v_out;
} ScalerParameter;

int DataFormatSoftwareConvert(cedarv_picture_t *picture, void* out_addr, int out_width, int out_height);

#ifdef __cplusplus
}
#endif

#endif  /* FORMAT_CONVERT_H_ */

