// 包含头文件
#include <stdlib.h>
#include <stdio.h>
#include "ffencoder.h"

int main(void)
{
    void   *encoder = NULL;
    static uint32_t abuf[1470     ];
    static uint32_t vbuf[320 * 240];
    void   *adata   [8] = { abuf };
    void   *vdata   [8] = { vbuf };
    int     linesize[8] = { 320 * 4 };
    int     i, j;

    printf("encode start\n");
    encoder = ffencoder_init(NULL);

    for (i=0; i<500; i++)
    {
        for (j=0; j<1470; j++) {
            abuf[j] = rand();
        }

        for (j=0; j<320*240; j++) {
            vbuf[j] = rand();
        }
        ffencoder_audio(encoder, adata, 1470    );
        ffencoder_video(encoder, vdata, linesize);
    }

    ffencoder_free(encoder);
    printf("encode done\n");
    return 0;
}
