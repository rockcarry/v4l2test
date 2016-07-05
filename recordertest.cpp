#include <cutils/properties.h>
#include "ffrecorder.h"

static uint64_t get_tick_count()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int main(int argc, char *argv[])
{
    void    *recorder = NULL;
    char     filea[128];
    char     fileb[128];
    char     filec[128];
    uint64_t curtick  = 0;
    int      i        = 0;

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
    ffrecorder_preview_window(recorder, 0, win);
    ffrecorder_preview_start (recorder, 0);

    // wait exit
    while (1) {
        if (get_tick_count() - curtick > 60 * 1000) {
            curtick = get_tick_count();
            sprintf(filea, "/sdcard/a_test%03d.mp4", i);
            sprintf(fileb, "/sdcard/b_test%03d.mp4", i);
            sprintf(filec, "/sdcard/c_test%03d.mp4", i); i++;
            ffrecorder_record_start(recorder, 0, filea);
            ffrecorder_record_start(recorder, 1, fileb);
//          ffrecorder_record_start(recorder, 2, filec);
            ffrecorder_record_start(recorder,-1, NULL );
        }

        char exit[PROP_VALUE_MAX];
        property_get("sys.ffrecorder.test.exit", exit, "0");
        if (strcmp(exit, "1") == 0) {
            break;
        }

        usleep(100*1000);
    }

    // stop record
    ffrecorder_record_stop(recorder, 0);
    ffrecorder_record_stop(recorder, 1);
//  ffrecorder_record_stop(recorder, 2);
    ffrecorder_record_stop(recorder,-1);

    // stop preview
    ffrecorder_preview_stop(recorder, 0);

    // close camdev
    ffrecorder_free(recorder);

    return 0;
}

