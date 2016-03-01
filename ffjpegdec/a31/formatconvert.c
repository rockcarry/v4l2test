
#define LOG_TAG "FormatConvert"
#include <cutils/log.h>

#include "formatconvert.h"

extern int sunxi_alloc_phy2vir(void *pbuf);

/*******************************************************************************
Function name: map32x32_to_yuv_Y
Description: 
    1. we should know : vdecbuf is 32*32 align
    2. must match gpuBuf size.
    3. we guarantee: vdecbufSize>=gpuBufSize
    4. if coded_width is stride, we can support gpu_buf_width 32byte align and 16byte align!
Parameters: 
    
Return: 
    
Time: 2013/4/15
*******************************************************************************/
static void map32x32_to_yuv_Y(unsigned char* srcY, unsigned char* tarY,unsigned int coded_width,unsigned int coded_height)
{
    int l;
    unsigned int i,j,m,n,k;
    unsigned int mb_width,mb_height,twomb_line, twomb_width, recon_width;
    unsigned long offset;
    unsigned char *ptr;
    unsigned char *dst_asm,*src_asm;
    unsigned vdecbuf_width, vdecbuf_height;
    int nWidthMatchFlag;
    int nLeftValidLine;  //in the bottom macroblock(32*32), the valid line is < 32.
    ptr = srcY;
    mb_width = ((coded_width+31)&~31) >>4;
    mb_height = ((coded_height+31)&~31) >>4;
    twomb_line = (mb_height+1)>>1;
    recon_width = (mb_width+1)&0xfffffffe;
    twomb_width = (mb_width+1)>>1;
    if (twomb_line < 1 || twomb_width < 1)
    {
        ALOGE("fatal error! twomb_line=%d, twomb_width=%d", twomb_line, twomb_width);
    }
    vdecbuf_width = twomb_width*32;
    vdecbuf_height = twomb_line*32;
    if (vdecbuf_width > coded_width)
    {
        nWidthMatchFlag = 0;
        if ((vdecbuf_width - coded_width) != 16)
        {
            ALOGW("(f:%s, l:%d) fatal error! vdecbuf_width=%d, gpubuf_width=%d,  the program will crash!", __FUNCTION__, __LINE__, vdecbuf_width, coded_width);
        }
        else
        {
            ALOGV("(f:%s, l:%d) Be careful! vdecbuf_width=%d, gpubuf_width=%d", __FUNCTION__, __LINE__, vdecbuf_width, coded_width);
        }
    }
    else if (vdecbuf_width == coded_width)
    {
        nWidthMatchFlag = 1;
    }
    else
    {
        ALOGW("(f:%s, l:%d) fatal error! vdecbuf_width=%d <= gpubuf_width=%d, the program will crash!", __FUNCTION__, __LINE__, vdecbuf_width, coded_width);
        nWidthMatchFlag = 0;
    }
    for (i=0; i<twomb_line-1; i++) //twomb line number
    {
        for (j=0; j<twomb_width-1; j++) //macroblock(32*32) number in one line
        {
            for (l=0; l<32; l++)
            {
                //first mb
                m = i*32 + l; //line num
                n = j*32;     //byte num in one line
                offset = m*coded_width + n;
                //memcpy(tarY+offset,ptr,32);
                dst_asm = tarY+offset;
                src_asm = ptr;
                asm volatile (
                    "vld1.8     {d0 - d3}, [%[src_asm]]     \n\t"
                    "vst1.8     {d0 - d3}, [%[dst_asm]]     \n\t"
                    : [dst_asm] "+r" (dst_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );

                ptr += 32;  //32 byte in one process.
            }
        }
        //process last macroblock of one line, gpu buf must be 16byte align or 32 byte align
        { //last mb of one line
            for (l=0; l<32; l++)
            {
                //first mb
                m = i*32 + l; //line num
                n = j*32;     //byte num in one line
                offset = m*coded_width + n;
                //memcpy(tarY+offset,ptr,32);
                dst_asm = tarY+offset;
                src_asm = ptr;
                if (nWidthMatchFlag)
                {
                    asm volatile (
                        "vld1.8     {d0 - d3}, [%[src_asm]]     \n\t"
                        "vst1.8     {d0 - d3}, [%[dst_asm]]     \n\t"
                        : [dst_asm] "+r" (dst_asm), [src_asm] "+r" (src_asm)
                        :  //[srcY] "r" (srcY)
                        : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                        );
                }
                else
                {
                    asm volatile (
                        "vld1.8     {d0,d1}, [%[src_asm]]       \n\t"
                        "vst1.8     {d0,d1}, [%[dst_asm]]       \n\t"
                        : [dst_asm] "+r" (dst_asm), [src_asm] "+r" (src_asm)
                        :  //[srcY] "r" (srcY)
                        : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                        );
                }

                ptr += 32;  //32 byte in one process.
            }
        }
    }
    //last twomb line, we process it alone
    nLeftValidLine = coded_height - (twomb_line-1)*32;
    if (nLeftValidLine!=32)
    {
        ALOGV("(f:%s, l:%d)hehehaha,gpuBufHeight[%d] is not 32 align", __FUNCTION__, __LINE__, nLeftValidLine);
    }
    for (j=0; j<twomb_width-1; j++)   //macroblock(32*32) number in one line
    {
        for (l=0; l<nLeftValidLine; l++)
        {
            //first mb
            m = i*32 + l; //line num
            n = j*32;     //byte num in one line
            offset = m*coded_width + n;
            //memcpy(tarY+offset,ptr,32);
            dst_asm = tarY+offset;
            src_asm = ptr;
            asm volatile (
                "vld1.8     {d0 - d3}, [%[src_asm]]     \n\t"
                "vst1.8     {d0 - d3}, [%[dst_asm]]     \n\t"
                : [dst_asm] "+r" (dst_asm), [src_asm] "+r" (src_asm)
                :  //[srcY] "r" (srcY)
                : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                );

            ptr += 32;  //32 byte in one process.
        }
        ptr += (32-nLeftValidLine)*32;
    }
    //process last macroblock of last line, gpu buf must be 16byte align or 32 byte align
    { //last mb of last line
        for (l=0;l<nLeftValidLine;l++)
        {
            //first mb
            m = i*32 + l; //line num
            n = j*32;     //byte num in one line
            offset = m*coded_width + n;
            //memcpy(tarY+offset,ptr,32);
            dst_asm = tarY+offset;
            src_asm = ptr;
            if (nWidthMatchFlag)
            {
                asm volatile (
                    "vld1.8     {d0 - d3}, [%[src_asm]]     \n\t"
                    "vst1.8     {d0 - d3}, [%[dst_asm]]     \n\t"
                    : [dst_asm] "+r" (dst_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
            }
            else
            {
                asm volatile (
                    "vld1.8     {d0,d1}, [%[src_asm]]       \n\t"
                    "vst1.8     {d0,d1}, [%[dst_asm]]       \n\t"
                    : [dst_asm] "+r" (dst_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
            }

            ptr += 32;  //32 byte in one process.
        }
        ptr += (32-nLeftValidLine)*32;
    }
}

/*******************************************************************************
Function name: map32x32_to_yuv_Y
Description: 
    1. we should know : vdecbuf_uv is 32*32 align too
    2. must match gpuBuf_uv size.
    3. gpuBuf_uv size is half of gpuBuf_y size
    4. we guarantee: vdecbufSize>=gpuBufSize
    5. uv's macroblock is also 16*16. Vdec_macroblock is also twomb(32*32).
    6. if coded_width is stride/2, we can support gpu_buf_width 16byte align and 8byte align!
       But need outer set right value of coded_width, must meet gpu_uv_buf_width align's request!
Parameters: 
    1. mode = 0:yv12, 1:thumb yuv420p
    2. coded_width and coded_height is uv size, already half of y_size
Return: 
    
Time: 2013/4/15
*******************************************************************************/
#define USE_VLD2_8  1
static void map32x32_to_yuv_C(int mode, unsigned char* srcC,unsigned char* tarCb,unsigned char* tarCr,unsigned int coded_width,unsigned int coded_height)
{
    int l;
    unsigned int i,j,m,n,k;
    unsigned int mb_width,mb_height,twomb_line,twomb_width,recon_width;
    unsigned long offset;
    unsigned char *ptr;
    unsigned char *dst0_asm,*dst1_asm,*src_asm;
    unsigned vdecbuf_width, vdecbuf_height; //unit: pixel
    int      nWidthMatchFlag;
    int      nLeftValidLine;  //in the bottom macroblock(32*32), the valid line is < 32.
    unsigned char line[16];
    int dst_stride = mode==0 ? (coded_width + 15) & (~15) : coded_width;

    ptr = srcC;
    mb_width = ((coded_width+15)&~15)>>4;   //vdec's uvBuf is 32byte align, so uBuf and vBuf is 16byte align!
    mb_height = ((coded_height+31)&~31)>>4;
    twomb_line = (mb_height+1)>>1;
    twomb_width = mb_width; //vdec mb32 is uv interleave, so uv_32 byte == u_16byte
    if (twomb_line < 1 || twomb_width < 1)
    {
        ALOGE("map32x32_to_yuv_C() fatal error! twomb_line=%d, twomb_width=%d", twomb_line, twomb_width);
    }
    //vdec mb32 uvBuf, one vdec_macro_block, extract u component, u's width and height.
    vdecbuf_width = twomb_width*16;
    vdecbuf_height = twomb_line*32;
    if (vdecbuf_width > coded_width)
    {
        nWidthMatchFlag = 0;
        if ((vdecbuf_width - coded_width) != 8)
        {
            ALOGW("(f:%s, l:%d) fatal error! vdec_UVbuf_width=%d, gpu_UVbuf_width=%d,  the program will crash!", __FUNCTION__, __LINE__, vdecbuf_width, coded_width);
        }
        else
        {
            ALOGV("(f:%s, l:%d) vdec_UVbuf_width=%d, gpu_UVbuf_width=%d, not match, gpu_uvBuf is 8byte align?", __FUNCTION__, __LINE__, vdecbuf_width, coded_width);
        }
    }
    else if (vdecbuf_width == coded_width)
    {
        nWidthMatchFlag = 1;
    }
    else
    {
        ALOGW("(f:%s, l:%d) fatal error! vdec_UVbuf_width=%d <= gpu_UVbuf_width=%d, the program will crash!", __FUNCTION__, __LINE__, vdecbuf_width, coded_width);
        nWidthMatchFlag = 0;
    }

    for (i=0; i<twomb_line-1; i++)
    {
        for (j=0; j<twomb_width-1; j++)
        {
            for (l=0; l<32; l++)
            {
                //first mb
                m = i*32 + l; //line num
                n = j*16;     //byte num in dst_one_line
                offset = m*dst_stride + n;

                dst0_asm = tarCb + offset;
                dst1_asm = tarCr+offset;
                src_asm = ptr;

#if (USE_VLD2_8 == 1)
            asm volatile (
                "vld2.8     {d0 - d3}, [%[src_asm]]     \n\t"
                "vst1.8     {d0,d1}, [%[dst0_asm]]      \n\t"
                "vst1.8     {d2,d3}, [%[dst1_asm]]      \n\t"
                : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                :  //[srcY] "r" (srcY)
                : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                );
#else
            asm volatile (
                "vld1.8     {d0 - d3}, [%[src_asm]]     \n\t"
                "vuzp.8     d0, d1                      \n\t"
                "vuzp.8     d2, d3                      \n\t"
                "vst1.8     {d0}, [%[dst0_asm]]!        \n\t"
                "vst1.8     {d2}, [%[dst0_asm]]!        \n\t"
                "vst1.8     {d1}, [%[dst1_asm]]!        \n\t"
                "vst1.8     {d3}, [%[dst1_asm]]!        \n\t"
                : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                :  //[srcY] "r" (srcY)
                : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                );
#endif
                ptr += 32;
            }
        }
        //process last twomb_macroblock of one line, gpu buf must be 16 byte align or 8 byte align.
        for (l=0; l<32; l++)
        {
            //first mb
            m = i*32 + l; //line num
            n = j*16;     //byte num in dst_one_line
            offset = m*dst_stride + n;

            dst0_asm = tarCb + offset;
            dst1_asm = tarCr+offset;
            src_asm = ptr;

            if (nWidthMatchFlag)
            {
#if (USE_VLD2_8 == 1)
                asm volatile (
                    "vld2.8     {d0 - d3}, [%[src_asm]]     \n\t"
                    "vst1.8     {d0,d1}, [%[dst0_asm]]      \n\t"
                    "vst1.8     {d2,d3}, [%[dst1_asm]]      \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#else
                asm volatile (
                    "vld1.8     {d0 - d3}, [%[src_asm]]     \n\t"
                    "vuzp.8     d0, d1                      \n\t"
                    "vuzp.8     d2, d3                      \n\t"
                    "vst1.8     {d0}, [%[dst0_asm]]!        \n\t"
                    "vst1.8     {d2}, [%[dst0_asm]]!        \n\t"
                    "vst1.8     {d1}, [%[dst1_asm]]!        \n\t"
                    "vst1.8     {d3}, [%[dst1_asm]]!        \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#endif
            }
            else
            {
#if (USE_VLD2_8 == 1)
                asm volatile (
                    "vld2.8     {d0,d1}, [%[src_asm]]       \n\t"
                    "vst1.8     {d0}, [%[dst0_asm]]         \n\t"
                    "vst1.8     {d1}, [%[dst1_asm]]         \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#else
                asm volatile (
                    "vld1.8     {d0,d1}, [%[src_asm]]       \n\t"
                    "vuzp.8     d0, d1                      \n\t"
                    "vst1.8     {d0}, [%[dst0_asm]]!        \n\t"
                    "vst1.8     {d1}, [%[dst1_asm]]!        \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#endif
            }
            ptr += 32;
        }
    }

    //last twomb line, we process it alone
    nLeftValidLine = coded_height - (twomb_line-1)*32;  //uv height can be odd number,must be very careful!
    if (nLeftValidLine!=32)
    {
        ALOGV("(f:%s, l:%d) hehehaha,gpu_UVBuf_extra_Height[%d] is not 32 align, coded_height[%d], twomb_line[%d]", 
            __FUNCTION__, __LINE__, nLeftValidLine, coded_height, twomb_line);
    }
    for (j=0; j<twomb_width-1; j++) //macroblock(32*32) number in one line
    {
        for (l=0; l<nLeftValidLine; l++)
        {
            //first mb
            m = i*32 + l; //line num
            n = j*16;     //byte num in dst_one_line
            offset = m*dst_stride + n;
            
            dst0_asm = tarCb + offset;
            dst1_asm = tarCr+offset;
            src_asm = ptr;

#if (USE_VLD2_8 == 1)
            asm volatile (
                "vld2.8     {d0 - d3}, [%[src_asm]]     \n\t"
                "vst1.8     {d0,d1}, [%[dst0_asm]]      \n\t"
                "vst1.8     {d2,d3}, [%[dst1_asm]]      \n\t"
                : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                :  //[srcY] "r" (srcY)
                : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                );
#else
            asm volatile (
                "vld1.8     {d0 - d3}, [%[src_asm]]     \n\t"
                "vuzp.8     d0, d1                      \n\t"
                "vuzp.8     d2, d3                      \n\t"
                "vst1.8     {d0}, [%[dst0_asm]]!        \n\t"
                "vst1.8     {d2}, [%[dst0_asm]]!        \n\t"
                "vst1.8     {d1}, [%[dst1_asm]]!        \n\t"
                "vst1.8     {d3}, [%[dst1_asm]]!        \n\t"
                : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                :  //[srcY] "r" (srcY)
                : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                );
#endif
            ptr += 32;  //32 byte in one process.
        }
        ptr += (32-nLeftValidLine)*32;
    }
    //process last macroblock of last line, gpu UVbuf must be 16byte align or 8 byte align
    { //last mb of last line
        for (l=0; l<nLeftValidLine; l++)
        {
            //first mb
            m = i*32 + l; //line num
            n = j*16;     //byte num in one line
            offset = m*dst_stride + n;

            dst0_asm = tarCb + offset;
            dst1_asm = tarCr+offset;
            src_asm = ptr;
            if (nWidthMatchFlag)
            {
#if (USE_VLD2_8 == 1)
                asm volatile (
                    "vld2.8     {d0 - d3}, [%[src_asm]]     \n\t"
                    "vst1.8     {d0,d1}, [%[dst0_asm]]      \n\t"
                    "vst1.8     {d2,d3}, [%[dst1_asm]]      \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#else
                asm volatile (
                    "vld1.8     {d0 - d3}, [%[src_asm]]     \n\t"
                    "vuzp.8     d0, d1                      \n\t"
                    "vuzp.8     d2, d3                      \n\t"
                    "vst1.8     {d0}, [%[dst0_asm]]!        \n\t"
                    "vst1.8     {d2}, [%[dst0_asm]]!        \n\t"
                    "vst1.8     {d1}, [%[dst1_asm]]!        \n\t"
                    "vst1.8     {d3}, [%[dst1_asm]]!        \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#endif
            }
            else
            {
#if (USE_VLD2_8 == 1)
                asm volatile (
                    "vld2.8     {d0,d1}, [%[src_asm]]       \n\t"
                    "vst1.8     {d0}, [%[dst0_asm]]         \n\t"
                    "vst1.8     {d1}, [%[dst1_asm]]         \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#else
                asm volatile (
                    "vld1.8     {d0,d1}, [%[src_asm]]       \n\t"
                    "vuzp.8     d0, d1                      \n\t"
                    "vst1.8     {d0}, [%[dst0_asm]]!        \n\t"
                    "vst1.8     {d1}, [%[dst1_asm]]!        \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#endif
            }

            ptr += 32;  //32 byte in one process.
        }
        ptr += (32-nLeftValidLine)*32;
    }
}

/*******************************************************************************
Function name: map32x32_to_yuv_C_422
Description: 
    1. mb32_422 to yv12_420
    2. so we discard one line in every two lines.
    3. mb32_yuv422, vdec_macroblock is also 32*32.
Parameters: 
    
Return: 
    
Time: 2013/4/16
*******************************************************************************/
static void map32x32_to_yuv_C_422(int mode, unsigned char* srcC,unsigned char* tarCb,unsigned char* tarCr,unsigned int coded_width,unsigned int coded_height)
{
    int l;
    unsigned int i,j,m,n,k;
    unsigned int mb_width,mb_height,twomb_line,twomb_width,recon_width;
    unsigned long offset;
    unsigned char *ptr;
    unsigned char *dst0_asm,*dst1_asm,*src_asm;
    unsigned char line[16];
    unsigned vdecbuf_width, vdecbuf_height;
    int nWidthMatchFlag;
    int nLeftValidLine;  //in the bottom macroblock(32*32), the valid line is < 32.

    ptr = srcC;
    mb_width = ((coded_width+15)&~15)>>4;   //vdec's uvBuf is 32byte align, so uBuf and vBuf is 16byte align!
    mb_height = ((coded_height*2+31)&~31)>>4;   //coded_height is gpu_buf's uv_height of yuv420, so uv_height of yuv422 must *2!
    twomb_line = (mb_height+1)>>1;
    twomb_width = mb_width; //vdec mb32 is uv interleave, so uv_32 byte == u_16byte
    if (twomb_line < 1 || twomb_width < 1)
    {
        ALOGE("map32x32_to_yuv_C_422() fatal error! twomb_line=%d, twomb_width=%d", twomb_line, twomb_width);
    }
    //vdec mb32 uvBuf, one vdec_macro_block, extract u component, u's width and height.
    vdecbuf_width = twomb_width*16;
    vdecbuf_height = twomb_line*32;
    if (vdecbuf_width > coded_width)
    {
        nWidthMatchFlag = 0;
        if ((vdecbuf_width - coded_width) != 8)
        {
            ALOGW("(f:%s, l:%d) fatal error! vdec_UVbuf_width=%d, gpu_UVbuf_width=%d,  the program will crash!", __FUNCTION__, __LINE__, vdecbuf_width, coded_width);
        }
        else
        {
            ALOGV("(f:%s, l:%d) vdec_UVbuf_width=%d, gpu_UVbuf_width=%d, not match, gpu_uvBuf is 8byte align?", __FUNCTION__, __LINE__, vdecbuf_width, coded_width);
        }
    }
    else if (vdecbuf_width == coded_width)
    {
        nWidthMatchFlag = 1;
    }
    else
    {
        ALOGW("(f:%s, l:%d) fatal error! vdec_UVbuf_width=%d <= gpu_UVbuf_width=%d, the program will crash!", __FUNCTION__, __LINE__, vdecbuf_width, coded_width);
        nWidthMatchFlag = 0;
    }

    for (i=0; i<twomb_line-1; i++)
    {
        for (j=0; j<twomb_width-1; j++)
        {
            for (l=0; l<16; l++)
            {
                //first mb
                m = i*16 + l; //line num, because yub422->yuv420, so discard one line in every two lines, so is 16!
                n = j*16;     //byte num in dst_one_line
                offset = m*coded_width + n;

                dst0_asm = tarCb + offset;
                dst1_asm = tarCr+offset;
                src_asm = ptr;

#if (USE_VLD2_8 == 1)
                 asm volatile (
                    "vld2.8     {d0 - d3}, [%[src_asm]]     \n\t"
                    "vst1.8     {d0,d1}, [%[dst0_asm]]      \n\t"
                    "vst1.8     {d2,d3}, [%[dst1_asm]]      \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#else
                 asm volatile (
                    "vld1.8     {d0 - d3}, [%[src_asm]]     \n\t"
                    "vuzp.8     d0, d1                      \n\t"
                    "vuzp.8     d2, d3                      \n\t"
                    "vst1.8     {d0}, [%[dst0_asm]]!        \n\t"
                    "vst1.8     {d2}, [%[dst0_asm]]!        \n\t"
                    "vst1.8     {d1}, [%[dst1_asm]]!        \n\t"
                    "vst1.8     {d3}, [%[dst1_asm]]!        \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#endif
                ptr += 32;
                ptr += 32;
            }
        }
        //process last twomb_macroblock of one line, gpu buf must be 16 byte align or 8 byte align.
        for (l=0; l<16; l++)
        {
            //first mb
            m = i*16 + l; //line num, because yub422->yuv420, so discard one line in every two lines, so is 16!
            n = j*16;     //byte num in dst_one_line
            offset = m*coded_width + n;

            dst0_asm = tarCb + offset;
            dst1_asm = tarCr+offset;
            src_asm = ptr;
            
            if (nWidthMatchFlag)
            {
#if (USE_VLD2_8 == 1)
                 asm volatile (
                    "vld2.8     {d0 - d3}, [%[src_asm]]     \n\t"
                    "vst1.8     {d0,d1}, [%[dst0_asm]]      \n\t"
                    "vst1.8     {d2,d3}, [%[dst1_asm]]      \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#else
                 asm volatile (
                    "vld1.8     {d0 - d3}, [%[src_asm]]     \n\t"
                    "vuzp.8     d0, d1                      \n\t"
                    "vuzp.8     d2, d3                      \n\t"
                    "vst1.8     {d0}, [%[dst0_asm]]!        \n\t"
                    "vst1.8     {d2}, [%[dst0_asm]]!        \n\t"
                    "vst1.8     {d1}, [%[dst1_asm]]!        \n\t"
                    "vst1.8     {d3}, [%[dst1_asm]]!        \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#endif
            }
            else
            {
#if (USE_VLD2_8 == 1)
                 asm volatile (
                    "vld2.8     {d0,d1}, [%[src_asm]]       \n\t"
                    "vst1.8     {d0}, [%[dst0_asm]]         \n\t"
                    "vst1.8     {d1}, [%[dst1_asm]]         \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#else
                 asm volatile (
                    "vld1.8     {d0,d1}, [%[src_asm]]       \n\t"
                    "vuzp.8     d0, d1                      \n\t"
                    "vst1.8     {d0}, [%[dst0_asm]]!        \n\t"
                    "vst1.8     {d1}, [%[dst1_asm]]!        \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#endif
            }
            ptr += 32;
            ptr += 32;
        }
    }

    //last twomb line, we process it alone
    nLeftValidLine = coded_height - (twomb_line-1)*16;  //uv height can be odd number,must be very careful! because yuv422, so 32line equal to 16 line of yuv420
    if (nLeftValidLine != 16)
    {
        ALOGV("(f:%s, l:%d) hehehaha,gpu_UVBuf_extra_Height[%d] is not 16 align, coded_height[%d], twomb_line[%d]", 
            __FUNCTION__, __LINE__, nLeftValidLine, coded_height, twomb_line);
    }
    for (j=0; j<twomb_width-1; j++)
    {
        for (l=0; l<nLeftValidLine; l++)
        {
            //first mb
            m = i*16 + l; //line num, because yub422->yuv420, so discard one line in every two lines, so is 16!
            n = j*16;     //byte num in dst_one_line
            offset = m*coded_width + n;

            dst0_asm = tarCb + offset;
            dst1_asm = tarCr+offset;
            src_asm = ptr;
            
#if (USE_VLD2_8 == 1)
             asm volatile (
                "vld2.8     {d0 - d3}, [%[src_asm]]     \n\t"
                "vst1.8     {d0,d1}, [%[dst0_asm]]      \n\t"
                "vst1.8     {d2,d3}, [%[dst1_asm]]      \n\t"
                : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                :  //[srcY] "r" (srcY)
                : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                );
#else
             asm volatile (
                "vld1.8     {d0 - d3}, [%[src_asm]]     \n\t"
                "vuzp.8     d0, d1                      \n\t"
                "vuzp.8     d2, d3                      \n\t"
                "vst1.8     {d0}, [%[dst0_asm]]!        \n\t"
                "vst1.8     {d2}, [%[dst0_asm]]!        \n\t"
                "vst1.8     {d1}, [%[dst1_asm]]!        \n\t"
                "vst1.8     {d3}, [%[dst1_asm]]!        \n\t"
                : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                :  //[srcY] "r" (srcY)
                : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                );
#endif
            ptr += 32;
            ptr += 32;
        }
        ptr += (16-nLeftValidLine)*32*2;
    }
    //process last twomb_macroblock of one line, gpu buf must be 16 byte align or 8 byte align.
    { //last mb of last line
        for (l=0; l<nLeftValidLine; l++)
        {
            //first mb
            m = i*16 + l; //line num, because yub422->yuv420, so discard one line in every two lines, so is 16!
            n = j*16;     //byte num in dst_one_line
            offset = m*coded_width + n;

            dst0_asm = tarCb + offset;
            dst1_asm = tarCr+offset;
            src_asm = ptr;
            
            if (nWidthMatchFlag)
            {
#if (USE_VLD2_8 == 1)
                 asm volatile (
                    "vld2.8     {d0 - d3}, [%[src_asm]]     \n\t"
                    "vst1.8     {d0,d1}, [%[dst0_asm]]      \n\t"
                    "vst1.8     {d2,d3}, [%[dst1_asm]]      \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#else
                 asm volatile (
                    "vld1.8     {d0 - d3}, [%[src_asm]]     \n\t"
                    "vuzp.8     d0, d1                      \n\t"
                    "vuzp.8     d2, d3                      \n\t"
                    "vst1.8     {d0}, [%[dst0_asm]]!        \n\t"
                    "vst1.8     {d2}, [%[dst0_asm]]!        \n\t"
                    "vst1.8     {d1}, [%[dst1_asm]]!        \n\t"
                    "vst1.8     {d3}, [%[dst1_asm]]!        \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#endif
            }
            else
            {
#if (USE_VLD2_8 == 1)
                 asm volatile (
                    "vld2.8     {d0,d1}, [%[src_asm]]       \n\t"
                    "vst1.8     {d0}, [%[dst0_asm]]         \n\t"
                    "vst1.8     {d1}, [%[dst1_asm]]         \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#else
                 asm volatile (
                    "vld1.8     {d0,d1}, [%[src_asm]]       \n\t"
                    "vuzp.8     d0, d1                      \n\t"
                    "vst1.8     {d0}, [%[dst0_asm]]!        \n\t"
                    "vst1.8     {d1}, [%[dst1_asm]]!        \n\t"
                    : [dst0_asm] "+r" (dst0_asm), [dst1_asm] "+r" (dst1_asm), [src_asm] "+r" (src_asm)
                    :  //[srcY] "r" (srcY)
                    : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
                    );
#endif
            }
            ptr += 32;
            ptr += 32;
        }
        ptr += (16-nLeftValidLine)*32*2;
    }
}

int SoftwarePictureScaler(ScalerParameter *cdx_scaler_para) {
    map32x32_to_yuv_Y(cdx_scaler_para->addr_y_in, (unsigned char*)cdx_scaler_para->addr_y_out, cdx_scaler_para->width_out, cdx_scaler_para->height_out);
    if (cdx_scaler_para->format_in == CONVERT_COLOR_FORMAT_YUV422MB) {
        ALOGV("meet mb_yuv422! cdx_scaler_para->format_in == CONVERT_COLOR_FORMAT_YUV422MB, format_out = CONVERT_COLOR_FORMAT_YUV420PLANNER");
        map32x32_to_yuv_C_422(cdx_scaler_para->mode,cdx_scaler_para->addr_c_in, (unsigned char*)cdx_scaler_para->addr_u_out, (unsigned char*)cdx_scaler_para->addr_v_out, cdx_scaler_para->width_out / 2, cdx_scaler_para->height_out / 2);
    }
    else {
        map32x32_to_yuv_C(cdx_scaler_para->mode,cdx_scaler_para->addr_c_in, (unsigned char*)cdx_scaler_para->addr_u_out, (unsigned char*)cdx_scaler_para->addr_v_out, cdx_scaler_para->width_out / 2, cdx_scaler_para->height_out / 2);
    }

    return 0;
}


static int ALIGN(int x, int y) {
    // y must be a power of 2.
    return (x + y - 1) & ~(y - 1);
}

int DataFormatSoftwareConvert(cedarv_picture_t *picture, void* out_addr, int out_width, int out_height) {
    int ret = 0;
    int dst_c_stride;
    int dst_y_size, dst_c_size;
    unsigned int    addr_y_out;
    unsigned int    addr_v_out;
    unsigned int    addr_u_out;
    ScalerParameter cdx_scaler_para;

    dst_y_size = out_width * out_height;
    dst_c_stride = ALIGN(out_width / 2, 16);
    dst_c_size = dst_c_stride * out_height / 2;

    cdx_scaler_para.width_out  = out_width;
    cdx_scaler_para.height_out = out_height;
    addr_y_out = (unsigned int)out_addr;
    addr_v_out = addr_y_out + dst_y_size;
    addr_u_out = addr_v_out + dst_c_size;
    cdx_scaler_para.addr_y_out = addr_y_out;
    cdx_scaler_para.addr_v_out = addr_v_out;
    cdx_scaler_para.addr_u_out = addr_u_out;

    cdx_scaler_para.mode = 0;
    cdx_scaler_para.format_in = (picture->pixel_format == CEDARV_PIXEL_FORMAT_MB_UV_COMBINE_YUV422) ? CONVERT_COLOR_FORMAT_YUV422MB : CONVERT_COLOR_FORMAT_YUV420MB;
    cdx_scaler_para.format_out = CONVERT_COLOR_FORMAT_YUV420PLANNER;
    cdx_scaler_para.width_in  = ALIGN(picture->width, 32);
    cdx_scaler_para.height_in = ALIGN(picture->height, 32);
    cdx_scaler_para.addr_y_in = (void*)sunxi_alloc_phy2vir(picture->y);
    cdx_scaler_para.addr_c_in = (void*)sunxi_alloc_phy2vir(picture->u);

    ret = SoftwarePictureScaler(&cdx_scaler_para);
    if (ret != 0) {
        ALOGE("Data Format Convert fail!");
    }
    
    return ret;
}


