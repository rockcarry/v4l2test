#ifndef __CEDARX_H264ENC_H__
#define __CEDARX_H264ENC_H__

// header files
#include <stdint.h>
#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// color type
enum
{
    BT601 = 0,
    BT709,
    YCC,
    VXYCC
};

// pixel format(yuv)
enum
{
    PIXEL_YUV444 = 0x10,    /* only used in scaler framebuffer */
    PIXEL_YUV422,
    PIXEL_YUV420,
    PIXEL_YUV411,
    PIXEL_CSIRGB,
    PIXEL_OTHERFMT,
    PIXEL_YVU420,
    PIXEL_YVU422,
    PIXEL_TILE_32X32,
    PIXEL_CSIARGB,
    PIXEL_CSIRGBA,
    PIXEL_CSIABGR,
    PIXEL_CSIBGRA,
    PIXEL_TILE_128X32,
    PIXEL_YUV420P,
    PIXEL_YVU420P,
    PIXEL_YUV422P,
    PIXEL_YVU422P,
    PIXEL_YUYV422,
    PIXEL_UYVY422,
    PIXEL_YVYU422,
    PIXEL_VYUY422
};

// ioctl cmd
enum
{
    VENC_SET_CSI_INFO_CMD = 1,
    VENC_SET_ENC_INFO_CMD,
    VENC_REGCB_CMD,
    VENC_INIT_CMD,
    VENC_GET_MOTION_FLAG,
    VENC_CROP_IMAGE_CMD,

    // star add
    VENC_REQUEST_KEY_FRAME_CMD,
    VENC_SET_BITRATE_CMD,
    VENC_SET_FRAMERATE_CMD,
    VENC_SET_ENCODE_MODE_CMD,
    VENC_SET_MAX_KEY_FRAME_INTERVAL,

    VENC_GET_SPS_PPS_DATA,
    VENC_QUERY_BSINFO,

    VENC_SET_RC_MODE,
    VENC_SET_MOTION_PAR_FLAG,
    VENC_SET_OUTSTREAM_BUF_LENGTH
};

// for get sps/pps data
typedef struct
{
    char *data;
    int   length;
} __data_container;

// codec config
typedef struct
{
    unsigned int   codec_type;
    int            width;
    int            height;

    unsigned int   frame_rate;
    int            color_format;
    int            color_space;
    int            qp_max;          // 40
    int            qp_min;          // 20
    int            avg_bit_rate;    // average bit rate
//  int            max_bit_rate;    // maximum bit rate

    int            maxKeyInterval;
    // define private information for video decode
//  unsigned int   video_bs_src;    // video bitstream source
//  void          *private_inf;     // video bitstream private information pointer
//  int            priv_inf_len;    // video bitstream private information length

    // star add
    unsigned char  profileIdc;
    unsigned char  levelIdc;

    unsigned int   src_width;
    unsigned int   src_height;

    int            scale_factor;
    double         focal_length;

    unsigned int   quality;         // for jpeg encoder
    unsigned int   orientation;     // for jpeg encoder

    // gps exif
    unsigned char  enable_gps;
    double         gps_latitude;   // input
    double         gps_longitude;  // input
    long           gps_altitude;
    long           gps_timestamp;
    char           gps_processing_method[100];
    int            whitebalance;
    unsigned int   thumb_width;
    unsigned int   thumb_height;
    unsigned int   ratioThreahold;

    unsigned char  CameraMake[64]; // for the cameraMake name
    unsigned char  CameraModel[64];// for the cameraMode
    unsigned char  DateTime[21];   // for the data and time
    unsigned char  picEncmode;     // 0 for frame encoding 1: for field encoding 2:field used for frame encoding
    int            rot_angle;      // 0,1,2,3
    // divide slices
    unsigned int   h264SliceEn;    // 0 for one slice;1 for several slice

    // for I frame temporal_filter
    unsigned int   I_filter_enable;
} __video_encode_format_t;

// input
typedef struct
{
    unsigned char *addrY;       // the luma component address for yuv format, or the address for bayer pattern format
    unsigned char *addrCb;      // the Cb component address for yuv format only.
    unsigned char *addrCr;      // the Cr component address for yuv format only
    unsigned int   color_fmt;
    unsigned int   color_space;
    long long      pts;         // unit:ms
    int            pts_valid;
    unsigned char *bayer_y;
    unsigned char *bayer_cb;
    unsigned char *bayer_cr;
    unsigned int  *Block_Header;// block header for olayer
    unsigned char *Block_Data;  // block data for olayer
    unsigned int  *Palette_Data;// palette data for olayer
    unsigned char  scale_mode;  // the THUMB scale_down coefficient
    void          *pover_overlay;
} __venc_frmbuf_info;

// output
typedef struct
{
    int            idx;
    unsigned char *pData0;
    int            uSize0;
    unsigned char *pData1;
    int            uSize1;
    long long      pts;
    int            pts_valid;
    unsigned char *privateData;
    int            privateDataLen;
    unsigned char  keyFrameFlag;
} __vbv_data_ctrl_info_t;

// VENC_DEVICE
typedef struct VENC_DEVICE
{
    void *priv_data;
    void *pIsp;
    int (*open )(struct VENC_DEVICE *p);
    int (*close)(struct VENC_DEVICE *p);
    int (*RequestBuffer)(struct VENC_DEVICE *pDev, unsigned char **pBuffery, unsigned char **pBufferC, unsigned int *phy_y, unsigned int *phy_c);
    int (*UpdataBuffer)(struct VENC_DEVICE *pDev);
    int (*hasOutputStream)(struct VENC_DEVICE *pDev);
    int (*encode)(struct VENC_DEVICE *pDev, void *pBuffer);
    int (*IoCtrl)(struct VENC_DEVICE *p, unsigned int, unsigned int);
    int (*GetBitStreamInfo)(struct VENC_DEVICE *pDev, __vbv_data_ctrl_info_t *pdatainfo);
    int (*ReleaseBitStreamInfo)(struct VENC_DEVICE *pDev, int node_id);
    int (*GetFrmBufCB )(int uParam1,  void *pFrmBufInfo);
    int (*WaitFinishCB)(int uParam1, void *pMsg);
} VENC_DEVICE;

int cedarx_hardware_init(int mode);
int cedarx_hardware_exit(int mode);

VENC_DEVICE *H264EncInit(int *ret);
int H264EncExit(VENC_DEVICE *pDev);

int  ion_alloc_open   (void);
int  ion_alloc_close  (void);
int  ion_alloc_alloc  (int   size);
void ion_alloc_free   (void *pbuf);
int  ion_alloc_vir2phy(void *pbuf);
int  ion_alloc_phy2vir(void *pbuf);
void ion_flush_cache  (void *pbuf, int size);
void ion_flush_cache_all(void);


typedef enum CEDARV_USAGE
{
    CEDARV_UNKNOWN           = 0,
    CEDARV_ENCODE_BACKGROUND = 1,
    CEDARV_DECODE_BACKGROUND = 2,
    CEDARV_ENCODE            = 3,
    CEDARV_DECODE            = 4,
} cedarv_usage_t;

typedef void* ve_mutex_t;

int  ve_mutex_init(ve_mutex_t *mutex, cedarv_usage_t usage);
void ve_mutex_destroy(ve_mutex_t *mutex);
int  ve_mutex_lock(ve_mutex_t *mutex);
int  ve_mutex_timed_lock(ve_mutex_t *mutex, int64_t timeout_us);
void ve_mutex_unlock(ve_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif


