#!/bin/bash
set -e

PREFIX_DIR=$PWD/ffmpeg-android-sdk
SYSROOT=$NDK_HOME/platforms/android-19/arch-arm/
CROSS_COMPILE=$NDK_HOME/toolchains/arm-linux-androideabi-4.9/prebuilt/windows/bin/arm-linux-androideabi-
EXTRA_CFLAGS="-I$PREFIX_DIR/include -DANDROID -DNDEBUG -Os -ffast-math -mfpu=neon-vfpv4 -mfloat-abi=softfp"
EXTRA_LDFLAGS="-L$PREFIX_DIR/lib"

# speed of faac is slower than ffmpeg native aac encoder now
# so we do not use faac
if false; then
#++ build faac ++#
if [ ! -d faac-1.28 ]; then
wget http://downloads.sourceforge.net/faac/faac-1.28.tar.gz
tar xvf faac-1.28.tar.gz
fi
CROSS_COMPILE=arm-linux-androideabi-
export CFLAGS="$CFLAGS"
export CPPFLAGS="$CFLAGS"
export CXXFLAGS="$CFLAGS"
export LDFLAGS="$LDFLAGS"
export CC="${CROSS_COMPILE}gcc"
export CXX="${CROSS_COMPILE}g++"
export NM="${CROSS_COMPILE}nm"
export LD="${CROSS_COMPILE}ld"
export STRIP="${CROSS_COMPILE}strip"
export AR="${CROSS_COMPILE}ar"
cd faac-1.28
./configure --prefix=$PWD/../ffmpeg-android-sdk \
--host=arm-linux \
--enable-static \
--enable-shared \
--without-mp4v2
make -j8 && make install
cd -
#-- build faac --#
fi

#++ build x264 ++#
if [ ! -d x264 ]; then
  git clone -b stable git://git.videolan.org/x264.git
fi
cd x264
./configure --prefix=$PREFIX_DIR \
--enable-strip \
--enable-static \
--enable-pic \
--disable-cli \
--disable-opencl \
--disable-avs \
--host=arm-linux-androideabi \
--cross-prefix=$CROSS_COMPILE \
--sysroot=$SYSROOT
make STRIP= -j8 && make install
cd -
#-- build x264 --#

#++ build ffmpeg ++#
if [ ! -d ffmpeg ]; then
  git clone -b fanplayer https://github.com/rockcarry/ffmpeg
fi
cd ffmpeg
./configure \
--pkg-config=pkg-config \
--arch=armv7 \
--cpu=armv7-a \
--target-os=android \
--enable-cross-compile \
--cross-prefix=$CROSS_COMPILE \
--sysroot=$SYSROOT \
--prefix=$PREFIX_DIR \
--enable-thumb \
--enable-static \
--enable-small \
--disable-shared \
--disable-symver \
--disable-debug \
--disable-programs \
--disable-doc \
--disable-avdevice \
--disable-avfilter \
--disable-postproc \
--disable-everything \
--disable-swscale-alpha \
--enable-encoder=libx264 \
--enable-encoder=aac \
--enable-encoder=mjpeg \
--enable-decoder=mjpeg \
--enable-muxer=avi \
--enable-muxer=mp4 \
--enable-protocol=file \
--enable-protocol=rtmp \
--enable-asm \
--enable-gpl \
--enable-version3 \
--enable-nonfree \
--enable-libx264 \
--extra-cflags="$EXTRA_CFLAGS" \
--extra-ldflags="$EXTRA_LDFLAGS"
make -j8 && make install
cd -
#++ build ffmpeg ++#

echo done

