#!/bin/bash

CFLAGS="-O3 -D__ARM_ARCH_7A__ -D__ANDROID__ -DNDEBUG"
EXTRA_CFLAGS="-march=armv7-a -I$PWD/include"
EXTRA_LDFLAGS="-Wl,--fix-cortex-a8 -L$PWD/lib"

#++ build x264 ++#
if [ ! -d x264 ]; then
  git clone git://git.videolan.org/x264.git
fi
cd x264
./configure --prefix=$PWD/.. \
--enable-strip \
--enable-static \
--enable-shared \
--host=arm-linux-androideabi \
--cross-prefix=arm-linux-androideabi- \
--extra-cflags="$CFLAGS $EXTRA_CFLAGS" \
--extra-ldflags="$EXTRA_LDFLAGS"
make -j8 && make install
cd -
#-- build x264 --#

if [ ! -d ffmpeg ]; then
  git clone git://source.ffmpeg.org/ffmpeg.git ffmpeg
fi
cd ffmpeg
./configure \
--arch=arm \
--target-os=android \
--enable-cross-compile \
--cross-prefix=arm-linux-androideabi- \
--prefix=$PWD/.. \
--enable-thumb \
--enable-static \
--enable-shared \
--enable-small \
--disable-symver \
--disable-debug \
--disable-programs \
--disable-doc \
--disable-avdevice \
--disable-avfilter \
--disable-postproc \
--disable-network \
--disable-everything \
--enable-encoder=mjpeg \
--enable-encoder=libx264 \
--enable-encoder=aac \
--enable-decoder=mjpeg \
--enable-decoder=h264 \
--enable-decoder=aac \
--enable-parser=mjpeg \
--enable-parser=h264 \
--enable-parser=aac \
--enable-demuxer=mjpeg \
--enable-demuxer=h264 \
--enable-demuxer=aac \
--enable-demuxer=mpegvideo \
--enable-muxer=mjpeg \
--enable-muxer=h264 \
--enable-muxer=mp4 \
--enable-protocol=file \
--disable-swscale-alpha \
--enable-asm \
--enable-gpl \
--enable-version3 \
--enable-nonfree \
--enable-libx264 \
--extra-cflags="$CFLAGS $EXTRA_CFLAGS" \
--extra-ldflags="$EXTRA_LDFLAGS"

make -j8 && make install

cd -

#rm -rf x264
#rm -rf ffmpeg
