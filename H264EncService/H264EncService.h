#ifndef __H264ENC_SERVICE_H__
#define __H264ENC_SERVICE_H__

#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>

using namespace android;

namespace android
{
    class IH264EncService : public IInterface {
    public:
        DECLARE_META_INTERFACE(H264EncService);
        virtual int  init(int iw, int ih, int ow, int oh, int frate, int bitrate) = 0;
        virtual void close(int handle) = 0;
        virtual int  encode(int handle, int64_t pts, void *phyy, void *phyc, int timeout, void *param) = 0;
    };

    enum {
        INIT = IBinder::FIRST_CALL_TRANSACTION,
        CLOSE,
        ENCODE,
    };

    // server class
    class BnH264EncService: public BnInterface<IH264EncService> {
    public:
        virtual status_t onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags = 0);
        virtual int  init(int iw, int ih, int ow, int oh, int frate, int bitrate);
        virtual void close(int handle);
        virtual int  encode(int handle, int64_t pts, void *phyy, void *phyc, int timeout, void *param);
    };

    // client class
    class BpH264EncService: public BpInterface<IH264EncService> {
    public:
        BpH264EncService(const sp<IBinder>& impl);
        virtual int  init(int iw, int ih, int ow, int oh, int frate, int bitrate);
        virtual void close(int handle);
        virtual int  encode(int handle, int64_t pts, void *phyy, void *phyc, int timeout, void *param);
    };
}

#endif




