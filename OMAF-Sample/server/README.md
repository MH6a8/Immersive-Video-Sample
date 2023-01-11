# ImmersiveVideo/OMAF-Sample/server镜像构建说明
文件路径：ImmersiveVideo/OMAF-Sample/server/

## 文件说明

1.  项目原文件

    + nginx_conf  
      nginx配置文件

    + CMakeLists.txt  
      cmake文件，作用是执行脚本deploy.sh start.sh stop.sh

    + deploy.sh  
      构建镜像脚本，可以修改镜像名称

    + deploy.yaml  
      部署镜像的配置文件，在start.sh中被引用

    + service.yaml  
      开启镜像服务的配置文件，同样在start.sh中被引用

    + start.sh  
      使用kubectl开启一个容器，暂未成功运行过，可以不管，不影响构建镜像

    + stop.sh  
      停止kubectl

    + run.sh
      在容器中使用，用于运行服务器

2.  修改的dockerfile以及第三方库文件

    + Dockerfile.base  
      基础镜像的dockerfile，第三方库已经本地化，并修改使用自己的ffmpeg

    + Dockerfile.runtime  
      运行服务器的镜像的dockerfile，基本只拷贝了基础镜像中的lib和bin文件

    + 3rdparty  
      包括了基础镜像Dockerfile.base需要的所有第三方库文件，除了ffmpeg
      
## 构建镜像
可同时参考官网README.md

1.  在当前文件夹安装自己的ffmpeg，例如
    ```
    su liangma4
    git clone -b n4.3.1_omaf_patched git@git.iflytek.com:HY_MetaLab/FFmpeg.git
    ```

2.  如需修改镜像名称，编辑deploy.sh中的IMAGEPREFIX和VERSION字段

3.  执行cmake命令
    ```bash
        cd path_to/Immersive-Video-Sample/OMAF-Sample/server
        mkdir build && cd build
        cmake .. -DHTTP_PROXY=<proxy> # proxy is optional
        make build -j $(nproc)
        docker image ls
        # REPOSITORY                                  TAG
        # immersive-server-gitlab-runtime             v4
        # immersive-server-gitlab-base                v4
    ``` 

## 运行容器

1.  由镜像构建容器，可使用端口映射
    ```
    docker run --privileged=true --user root --name=容器名称 -p 30071:1483 -p 30072:8080 -it 镜像ID bash
    ```

2.  运行nginx服务器
    ```
    cd /usr/local/nginx/conf/
    ./configure.sh CN Shanghai A B C D E@F.com
    /usr/local/nginx/sbin/nginx
    ```

3.  执行run.sh，使用ffmpeg推流（命令待修改，使用SVT-HEVC编码器在debug中）
    ```
    cd /home/immersive/Sample-Videos && ./run.sh <RES> <TYPE>
    # <RES>:[4K,8K] <TYPE>:[LIVE,VOD]
    ```
