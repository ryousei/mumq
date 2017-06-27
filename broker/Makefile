# Copyright (C) 2016-2017 National Institute of Advanced Industrial Science 
# and Technology, Mahidol University
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

CFLAG = -std=gnu99 -Wall
LFLAG = -lpthread
DBGFLAG = -g -DDEBUG

CFLAG += $(DBGFLAG)

DPDK_INC = ../mtcp/dpdk/include
DPDK_LIB = ../mtcp/dpdk/lib

MTCP_INC = ../mtcp/mtcp/include
MTCP_LIB = ../mtcp/mtcp/lib -lmtcp

UTIL_INC = ../../mtcp/util/include

DPDK_MACHINE_FLAGS = $(shell cat ../mtcp/dpdk/include/cflags.txt)
DPDK_LIB_FLAGS = $(shell cat ../mtcp/dpdk/lib/ldflags.txt)

all : broker util

mtcp: LFLAG += -m64 -O3 -pthread -lrt -march=native -Wl,-export-dynamic -L${DPDK_LIB} -Wl,--no-as-needed,-lnuma -Wl,-lpthread -Wl,-lrt -Wl,-ldl -Wl,${DPDK_LIB_FLAGS} -L${MTCP_LIB} 
mtcp: CFLAG += -DMTCP
mtcp: INC += ${DPDK_MACHINE_FLAGS} -I${DPDK_INC} -include $(DPDK_INC)/rte_config.h -I${MTCP_INC} -I${UTIL_INC}
mtcp: all

broker : broker.o mqtt.o hand_mqtt.o substree.o utility.o event_io.o
	gcc $^ $(LFLAG) -o $@

broker.o : broker.c
	gcc $(CFLAG) -c $< -o $@ $(INC)

utility.o : utility.c
	gcc $(CFLAG) -c $< -o $@ $(INC)

event_io.o : event_io.c
	gcc $(CFLAG) -c $< -o $@ $(INC)

mqtt.o : mqtt.c
	gcc $(CFLAG) -c $< -o $@ $(INC)

hand_mqtt.o : hand_mqtt.c
	gcc $(CFLAG) -c $< -o $@ $(INC)

substree.o : substree.c
	gcc $(CFLAG) -c $< -o $@ $(INC)

util :
	gcc getsockop.c -o getsockop

clean:
	rm -f *.o broker getsockop log_*
