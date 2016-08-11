package com.apical.dvr;

public class MediaRecorder {

    public static native long nativeInit();
    public static native void nativeFree(long ctxt);

    public static native int  nativeGetMicMute (long ctxt, int micidx);
    public static native void nativeSetMicMute (long ctxt, int micidx, int mute);
    public static native void nativeResetCamera(long ctxt, int camidx, int w, int h, int frate);

    public static native void nativeSetPreviewWindow(long ctxt, int camidx, Object win);
    public static native void nativeSetPreviewTarget(long ctxt, int camidx, Object win);

    public static native void nativeStartPreview(long ctxt, int camidx);
    public static native void nativeStopPreview (long ctxt, int camidx);

    public static native void nativeStartRecording(long ctxt, int encidx, String filename);
    public static native void nativeStopRecording (long ctxt, int encidx);

    public static native void nativeSetAudioSource(long ctxt, int encidx, int source);
    public static native void nativeSetVideoSource(long ctxt, int encidx, int source);

    static {
        System.loadLibrary("ffrecorder_jni");
    }
}



