https://github.com/OpenVisualCloud/SVT-HEVC/tree/master/ffmpeg_plugin

关键把.so和.pc文件移到相应位置，再把ffmpeg复制
    cp -f ../SVT-HEVC/Build/linux/Release/SvtHevcEnc.pc /usr/local/lib/pkgconfig
    cp -f ../SVT-HEVC/Bin/Release/libSvtHevcEnc.so.1 /usr/local/lib
    cp -f ../SVT-HEVC/Bin/Release/libSvtHevcEnc.so /usr/local/lib

docker run --privileged=true --user root -p 30067:1483 -p 30068:8080 -it immersive-server-develop:feature_svt bash

DOCKER_BUILDKIT=1 docker build --network=host --tag immersive-server-develop:feature_svt --build-arg "base_image=immersive-server-base:feature_svt" --file Dockerfile.runtime .

https://github.com/OpenVisualCloud/SVT-HEVC/blob/master/Docs/svt-hevc_encoder_user_guide.md

export LD_LIBRARY_PATH=/usr/local/lib/:/usr/local/lib64:$LD_LIBRARY_PATH

    ./ffmpeg -re -stream_loop -1 \
        -i ./test1_h265_3840x2048_30fps_30M_200frames.mp4 -rc 1 \
        -c:v:0 libsvt_hevc \
        -s:0 3840x1920 \
        -tile_row_cnt:0 6 -tile_col_cnt:0 12 \
        -socket:0 0 \
        -la_depth:0 0 -r:0 30 -g:0 15 \
        -b:0 30M -map 0:v \
        -s:1 1024x640 -sws_flags neighbor \
        -tile_row_cnt:1 2 -tile_col_cnt:1 2 \
        -socket:1 1 \
        -la_depth:1 0 -r:1 30 -g:1 15 \
        -b:1 5M -map 0:v -vframes 1000000 \
        -f omaf_packing \
        -window_size 20 -extra_window_size 30 \
        -base_url http://172.30.95.218:30002/LIVE4K/ \
        -out_name Test /usr/local/nginx/html/LIVE4K/

# -c:0 ../../Sample-Videos/config_high.xml \
# -c:1 ../../Sample-Videos/config_low.xml \

修改install_FFmpeg.sh和Dockerfile.base

install_FFmpeg.sh中加入ffmpeg需要的patches
```
git am ../ffmpeg/patches/n4.4-0001-lavc-svt_hevc-add-libsvt-hevc-encoder-wrapper.patch
```

Dockerfile.base中将SVT-HEVC部分文件复制到相应路径
```
# Install SVT
# git clone https://github.com/OpenVisualCloud/SVT-HEVC.git && \
RUN cd SVT-HEVC && \
    source /opt/rh/devtoolset-7/enable && \
    git checkout ec0d95c7e0d5be20586e1b87150bdfb9ae97cf4d && \
    cd Build/linux/ && \
    ./build.sh && \
    cd Release && \
    make install && \
    cp SvtHevcEnc.pc /usr/local/lib/pkgconfig/ && \
    cd ${WORKDIR}/SVT-HEVC/Bin/Release && \
    cp libSvtHevcEnc.so.1 /usr/local/lib/ && \
    cd ${WORKDIR} && rm -rf ./SVT-HEVC
```




用官网的libsvt_hevc.c替换OMAF中patch过后的libsvt_hevc.c

export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:/usr/local/lib
export PKG_CONFIG_PATH=${PKG_CONFIG_PATH}:/usr/local/lib/pkgconfig

./configure --prefix=/usr --libdir=/usr/lib --enable-static --enable-debug \
                  --disable-shared --enable-gpl --enable-nonfree \
                  --disable-optimizations --disable-vaapi \
                  --enable-libopenhevc \
                  --disable-libDistributedEncoder \
                  --enable-libVROmafPacking \
                  --enable-libsvthevc

./configure --prefix=/usr --libdir=/usr/lib --enable-static \
                  --disable-shared --enable-gpl --enable-nonfree \
                  --disable-optimizations --disable-vaapi --enable-libopenhevc \
                  --disable-libDistributedEncoder --enable-libsvthevc --enable-libVROmafPacking \
                  --disable-filter=erp2cubmap_mdf 


按照install_SVT内的步骤编译会报错



omaf_packing_enc.c存在问题，导致段错误

export LD_LIBRARY_PATH=/usr/local/lib/:/usr/local/lib64:$LD_LIBRARY_PATH

./ffmpeg -i ../../test1_h265_3840x2048_30fps_30M_200frames.mp4 -c:v libsvt_hevc -f omaf_packing -out_name Test /usr/local/nginx/html/


