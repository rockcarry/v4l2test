// 包含头文件
#include <stdlib.h>
#include "micdev.h"

// 函数实现
void* micdev_init(int samprate, int sampsize, int h)
{
    return NULL;
}

void micdev_close(void *dev) {}
void micdev_start_capture(void *dev) {}
void micdev_stop_capture (void *dev) {}
void micdev_set_encoder  (void *dev, void *encoder) {}

