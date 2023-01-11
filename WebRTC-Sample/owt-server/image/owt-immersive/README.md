# Immersive-Video-Sample/WebRTC-Sample/owt-server镜像构建说明
文件路径：Immersive-Video-Sample/WebRTC-Sample/owt-server/image/owt-immersive/

## 文件说明

1.  项目原文件

    + scripts  
      会被复制到容器中，用于运行项目
    + CMakeLists.txt  
      cmake文件
    + build_original.sh  
      原版的构建脚本，原名称为build.sh，如要执行需修改内容
    + Dockerfile_original  
      原版Dockerfile，原名称为Dockerfile  

2.  核心源代码及第三方库文件

    + owt-server  
      owt服务器的源代码
    + owt-client-javascript  
      owt客户端源代码
    + webrtc-src  
      webrtc部分源代码
    + Sample-Videos  
      样例视频
    + 3rdparty  
      提前导入dockerfile的第三方库文件  

3.  Dockerfile和对应脚本

    + Dockerfile_base  
      为基础镜像，用于安装一些基本工具和不太会改动的第三方库
    + build_base.sh  
      基础镜像构建脚本
    + Dockerfile_develop  
      为开发镜像，修改了owt-server等内容后，需要重新构建镜像  
      注意开发镜像中也分为了两层，一层安装owt-server等，一层安装node、mongod、rabbitmq等工具，形成一个发布版本镜像
    + build_develop.sh  
      开发镜像构建脚本

## 构建镜像

1.  安装自己的ffmpeg和owt-server

    + 在当前目录下git获取ffmpeg和owt-server，注意使用自己的账户并指定分支，例如
    + su liangma4
    + git clone -b n4.1_owt_patched git@git.iflytek.com:HY_MetaLab/FFmpeg.git
    + git clone git@git.iflytek.com:HY_MetaLab/ImmersiveVideo-owt-server.git  
    
2.  构建基础镜像

    + 执行脚本build_base.sh
    + bash build_base.sh
    + 构建时间约为30min
    + 如果需要修改生成的镜像名称，则编辑脚本的IMAGE字段  

3.  构建开发镜像

    + 执行脚本build_develop.sh
    + bash build_develop.sh
    + 构建时间约为22min
    + 如果需要修改生成的镜像名称，则编辑脚本的IMAGE字段和BASETAG字段，其中BASETAG字段与基础镜像保持一致  

## 运行容器

1.  由镜像生成容器

    + 不做端口映射使用如下命令（可能产生端口冲突）
    + docker run --net=host --privileged=true -it 镜像ID bash
    + 需要端口映射则用
    + docker run -p 3010-3014:3000-3004 -p 3400:3300 -p 8099:8080 -p 5772:5672 -p 27667:27009 -it 镜像ID bash
    + 其中冒号左边为宿主机端口，也就是外界访问的端口，冒号右边是容器中使用的端口  

2.  运行服务器

    + 执行以下脚本即可  
    ```
    bash /home/init.sh && /home/start.sh && /home/restApi.sh -s 4k && /home/restart.sh && /home/restApi.sh -c /home/Sample-Videos/test1_h265_3840x2048_30fps_30M_200frames.mp4 && /home/sleep.sh
    ```


