package com.apical.dvr;

public class MediaRecorder {

    private static MediaRecorder mSingleInstance = null;

    public static MediaRecorder getInstance() {
        if (mSingleInstance == null) {
            mSingleInstance = new MediaRecorder();
        }
        return mSingleInstance;
    }

    private long mRecorderContext;

    public void init() {
        mRecorderContext = nativeInit();
        nativeInitCallback(mRecorderContext);
    }

    public void release() {
        nativeFree(mRecorderContext);
        mSingleInstance = null;
    }

    public boolean getMicMute(int micidx) {
        int mute = nativeGetMicMute(mRecorderContext, micidx);
        return (nativeGetMicMute(mRecorderContext, micidx) == 1);
    }

    public void setMicMute(int micidx, boolean mute) {
        nativeSetMicMute(mRecorderContext, micidx, mute ? 1 : 0);
    }

    public void resetCamera(int camidx, int w, int h, int frate) {
        nativeResetCamera(mRecorderContext, camidx, w, h, frate);
    }

    public void setPreviewDisplay(int camidx, Object win) {
        nativeSetPreviewWindow(mRecorderContext, camidx, win);
    }

    public void setPreviewTexture(int camidx, Object win) {
        nativeSetPreviewTarget(mRecorderContext, camidx, win);
    }

    public void startPreview(int camidx) {
        nativeStartPreview(mRecorderContext, camidx);
    }

    public void stopPreview(int camidx) {
        nativeStopPreview(mRecorderContext, camidx);
    }

    public void startRecording(int encidx, String filename) {
        nativeStartRecording(mRecorderContext, encidx, filename);
    }

    public void stopRecording(int encidx) {
        nativeStopRecording(mRecorderContext, encidx);
    }

    public void setAudioSource(int encidx, int source) {
        nativeSetAudioSource(mRecorderContext, encidx, source);
    }

    public void setVideoSource(int encidx, int source) {
        nativeSetVideoSource(mRecorderContext, encidx, source);
    }

    public void takePhoto(int camidx, String filename, takePhotoCallback callback) {
        mTakePhotoCB = callback; // setup callback
        nativeTakePhoto(mRecorderContext, camidx, filename);
    }

    public interface takePhotoCallback {
        public void onPhotoTaken(String filename);
    }


    //++ for take photo callback
    private takePhotoCallback mTakePhotoCB = null;

    private void internalTakePhotoCallback(String filename) {
        if (mTakePhotoCB != null) {
            mTakePhotoCB.onPhotoTaken(filename);
        }
    }

    private native void nativeInitCallback(long ctxt);
    //-- for take photo callback


    private static native long nativeInit();
    private static native void nativeFree(long ctxt);

    private static native int  nativeGetMicMute (long ctxt, int micidx);
    private static native void nativeSetMicMute (long ctxt, int micidx, int mute);
    private static native void nativeResetCamera(long ctxt, int camidx, int w, int h, int frate);

    private static native void nativeSetPreviewWindow(long ctxt, int camidx, Object win);
    private static native void nativeSetPreviewTarget(long ctxt, int camidx, Object win);

    private static native void nativeStartPreview(long ctxt, int camidx);
    private static native void nativeStopPreview (long ctxt, int camidx);

    private static native void nativeStartRecording(long ctxt, int encidx, String filename);
    private static native void nativeStopRecording (long ctxt, int encidx);

    private static native void nativeSetAudioSource(long ctxt, int encidx, int source);
    private static native void nativeSetVideoSource(long ctxt, int encidx, int source);

    private static native void nativeTakePhoto(long ctxt, int camidx, String filename);

    static {
        System.loadLibrary("ffrecorder_jni");
    }
}



