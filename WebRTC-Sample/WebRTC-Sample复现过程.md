# 一、搭建owt-server

1. 进入Immersive-Video-Sample/WebRTC-Sample/owt-server/image/owt-immersive/
   执行bash build.sh

2. 复制一份demo视频
   docker cp Immersive-Video-Sample/Sample-Videos/test1_h265_3840x2048_30fps_30M_200frames.mp4 容器ID:/home/

3. 由镜像运行容器并进入，然后执行脚本
   docker run -it --net=host --privileged=true 镜像ID bash
   bash /home/init.sh && /home/start.sh && /home/restApi.sh -s 4k && /home/restart.sh && /home/restApi.sh -c /home/Sample-Videos/test1_h265_3840x2048_30fps_30M_200frames.mp4 && /home/sleep.sh
   服务器保持运行

注意：开启多个服务器时，需要修改/etc/mongod.conf中的端口号
      port: 27017
      例如port: 27009

      出现类似报错ERROR: node with name "rabbit" already running on "7664475cc917"
      说明rabbitmq-server已启动，在init.sh中注释该命令即可

      出现报错
      2022-11-29T02:41:47.810+0000 W NETWORK  [thread1] Failed to connect to 127.0.0.1:27017, in(checking socket for error after poll), reason: Connection refused
      2022-11-29T02:41:47.811+0000 E QUERY    [thread1] Error: couldn't connect to server 127.0.0.1:27017, connection attempt failed :
      connect@src/mongo/shell/mongo.js:275:13
      @(connect):1:21
      exception: connect failed
      第一次由镜像run开启容器便会报错，退出容器stop并重新使用exec进入便不会报错
      同时注意创建时run的选项齐全
      真正解决办法：
      init.sh内修改为
      mongo --port 27009 --quiet --eval "db.adminCommand('listDatabases')"
      即指定端口号
      同时修改
      ./management_api/init.sh --dburl=127.0.0.1:27009/owtdb
      即可初始化成功

      bash start.sh后，出现端口号报错，逐一修改

      <!-- vi /home/owt/management_console/index.js
      第20行 3300 -> 3308
      第21行 3000 -> 3008 --> #存疑，没有每次出现

      修改mongod相关的端口号
      vi /home/owt/management_api/management_api.toml
      vi /home/owt/sip_portal/sip_portal.toml
      vi /home/owt/portal/portal.toml
      dataBaseURL = "localhost/owtdb" #default: "localhost/owtdb"改为
      dataBaseURL = "127.0.01:27009/owtdb" #default: "localhost/owtdb"

      vim /home/owt/portal/portal.toml
      port = 8080 #default: 8080
      改为
      port = 8098 #default: 8080

      vim /home/owt/management_console/management_console.toml
      port = 3300 #default: 3300, port of manage console server
      改为
      port = 3308 #default: 3300, port of manage console server

      vim /home/owt/extras/basic_example/samplertcservice.js
      542行
      }, app).listen(3001, (err) => {
      改为
      }, app).listen(3009, (err) => {
      555行
      }, app).listen(3004, (error) => {
      改为
      }, app).listen(3010, (error) => {
      
      bash restApi.sh，修改端口号
      vim restApi.sh
      DB_URL='localhost/owtdb'改为
      DB_URL='127.0.0.1:27009/owtdb'

      仍未正常运行

      正确方法：
      docker run -it -p 3010-3014:3000-3004 -p 3400:3300 -p 8099:8080 -p 5772:5672 -p 27667:27009 --privileged=true 镜像ID bash

      



# 二、搭建owt-linux-player

编译略过

1. 进入Immersive-Video-Sample-master/WebRTC-Sample/owt-linux-player/player，修改config.xml
   <server_url>改为服务器ip及端口号，例如
   http://172.30.95.218:3001

2. 执行脚本setupvars.sh，如未生效，终端输入
   export LD_LIBRARY_PATH=/data1/liangma/Code/IV/mjchen/Immersive-Video-Sample-master/WebRTC-Sample/owt-linux-player/webrtc_linux_client_sdk/release/lib:$LD_LIBRARY_PATH
   或正确执行脚本，source ./set_ld_path.sh

3. 执行./render，出现窗口，按s开始播放，上下左右键移动视角