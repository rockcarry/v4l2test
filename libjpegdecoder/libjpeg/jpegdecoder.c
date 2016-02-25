// 包含头文件
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <setjmp.h>
#include "jpeglib.h"
#include "../jpegdecoder.h"

struct sf_jpeg_error_mgr {
    struct jpeg_error_mgr jerr;
    jmp_buf longjmp_buffer;
};

static void sf_jpeg_error_exit(j_common_ptr cinfo) {
    struct sf_jpeg_error_mgr *sf_err = (struct sf_jpeg_error_mgr *)cinfo->err;
    longjmp(sf_err->longjmp_buffer, 0);
}

// 函数实现
void* jpeg_decoder_init(void)
{
    return NULL;
}

void jpeg_decoder_decode(void *decoder, void *buf, int len, int pts)
{
}

void jpeg_decoder_getframe(void *decoder, void *buf, int w, int h)
{
}

void jpeg_decoder_free(void *decoder)
{
}

