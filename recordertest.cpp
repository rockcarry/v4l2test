#include <cutils/properties.h>
#include "ffrecorder.h"

int main(int argc, char *argv[])
{
    void *recorder = NULL;
    char  file[128];
    int   i = 0;

    // set exit flag to 0
    property_set("sys.ffrecorder.test.exit", "0");

    // create a client to surfaceflinger
    sp<SurfaceComposerClient> client = new SurfaceComposerClient();
    sp<IBinder> dtoken(SurfaceComposerClient::getBuiltInDisplay(
            ISurfaceComposer::eDisplayIdMain));

    DisplayInfo dinfo;
    status_t status = SurfaceComposerClient::getDisplayInfo(dtoken, &dinfo);
    if (status) {
        return -1;
    }

    sp<SurfaceControl> surfaceControl = client->createSurface(String8("yuvtest_surface"),
            dinfo.w, dinfo.h, PIXEL_FORMAT_RGBX_8888, 0);

    SurfaceComposerClient::openGlobalTransaction();
    surfaceControl->setLayer(0x40000000);
//  surfaceControl->setPosition(0, 0);
//  surfaceControl->setSize(320, 180);
    SurfaceComposerClient::closeGlobalTransaction();

    sp<Surface>   surface = surfaceControl->getSurface();
    sp<ANativeWindow> win = surface;

    // init camdev
    recorder = ffrecorder_init(NULL);

    // startpreview
    ffrecorder_preview_window(recorder, win);
    ffrecorder_preview_start (recorder);

    // start record
    ffrecorder_record_start(recorder, (char*)"/sdcard/test0.mp4");

    // wait exit
    while (1) {
        char exit[PROP_VALUE_MAX];
        property_get("sys.ffrecorder.test.exit", exit, "0");
        if (strcmp(exit, "1") == 0) {
            break;
        }

        if (++i % 600 == 0) {
            sprintf(file, "/sdcard/test%d.mp4", i / 600);
            ffrecorder_record_start(recorder, file);
        }
        usleep(100*1000);
    }

    // stop record
    ffrecorder_record_stop(recorder);

    // stop preview
    ffrecorder_preview_stop(recorder);

    // close camdev
    ffrecorder_free(recorder);

    return 0;
}

