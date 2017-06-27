# README #

This is a repository of muMQ: A lightweight and scalable MQTT broker. muMQ source code is distributed under Apache License Version 2.0.

### Prerequisite ###
* ``libpthread`` is required for multi-threading
* ``mTCP`` is not included in this repository. Please download from [mTCP page](https://github.com/eunyoung14/mtcp)

### Directories ###
* broker: muMQ source code and configuration files
     - broker/config: Place for arp.conf and route.conf
* tools: MQTT testing tool source code

### Install Guides ###
muMQ can be built to run on two kinds of the TCP/IP stack.  

1) Linux kernel TCP/IP stack:  

- Run make under `broker/`  

     ``# cd broker/``  

     ``# make``  

2) mTCP with DPDK:  

- Downlowd mTCP project under `broker/`  

- Build mTCP library by following INSTALL GUIDES of DPDK version on [README.md](https://github.com/eunyoung14/mtcp/blob/master/README.md) of mTCP  

- Make sure mTCP works by running mTCP example apps  

- Run make with mtcp argument under `broker/`  

     ``# cd broker/``  

     ``# make mtcp``  

- Modify arp.conf and route.conf under `mtcp/config/` according to your environment and copy them to `broker/config/`  

     ``# cp mtcp/config/sample_arp.conf broker/config/arp.conf``  

     ``# cp mtcp/config/sample_route.conf broker/config/route.conf``  

- Modify mtcp.conf and copy to `broker/`  

     ``# cp mtcp/config/sample_mtcp.conf broker/broker.conf``  

### Run Application ###

- Simple run a executable file with port number  

     ``# ./broker 1883``  

- Note CPU scalability can be configured in `broker/config.h`

     ``     #define LOOP_COUNT 16``  

### Authors ###

* Wiriyang Pipatsakulroj (Mahidol University, Thailand)

### License ###

* Apache License Version 2.0

### References ###

* Wiriyang Pipatsakulroj, Vasaka Visoottiviseth, Ryousei Takano, "muMQ: A Lightweight and Scalable MQTT Broker", The 23rd IEEE International Symposium on Local and Metropolitan Area Networks (LANMAN), June 2017
* [Cyber Phsical Cloud Research Group, AIST](http://www.itri.aist.go.jp/cpc/)

### Acknowledgement ###

This work was partially done during an internship at AIST, Japan.
