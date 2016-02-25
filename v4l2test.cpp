#include <cutils/properties.h>
#include "usbcam.h"

int main(int argc, char *argv[])
{
    USBCAM *cam = NULL;
    char    dev[32];

    if (argc < 2) {
        strcpy(dev, "/dev/video0");
    }
    else {
        strcpy(dev, argv[1]);
    }

    // set exit flag to 0
    property_set("sys.v4l2.test.exit", "0");

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
//  surfaceControl->setSize(dinfo.w, dinfo.h);
    SurfaceComposerClient::closeGlobalTransaction();

    sp<Surface> surface = surfaceControl->getSurface();
    sp<ANativeWindow> win = surface;

    // init usbcam
    cam = usbcam_init(dev);

    // startpreview
    usbcam_set_preview_window(cam, win, dinfo.w, dinfo.h);
    usbcam_start_preview(cam);

    // wait exit
    while (1) {
        char exit[PROP_VALUE_MAX];
        property_get("sys.v4l2.test.exit", exit, "0");
        if (strcmp(exit, "1") == 0) {
            break;
        }
    }

    // stoppreview
    usbcam_stop_preview(cam);

    // close usbcam
    usbcam_close(cam);

    return 0;
}

