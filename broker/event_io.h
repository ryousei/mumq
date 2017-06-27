/*
* Copyright (C) 2016-2017 National Institute of Advanced Industrial Science 
* and Technology, Mahidol University
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*        http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef EVENT_IO_H
#define EVENT_IO_H

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <sys/socket.h>
#include "uthash.h"
#ifdef MTCP
#include "mtcp_epoll.h"
#include <pthread.h>
#else
#include <sys/epoll.h>
#endif

#include "config.h"
#include "client.h"

struct epollev {
    void *arg;
    int events;
    int thread_id;
};

struct epollop {
#if MTCP
    mctx_t mctx;
#endif
    uint32_t epfd;
    int num_events;
    struct epollev *fds;
    int l_sock;
    int base_sock;
    int th_id;
    
    struct client *contexts_by_ptr;
};

void modify_event(struct epollop* epoll_op, int fd, int events);
void disconnect_sock(struct epollop* epoll_op, int fd);
int add_sock(struct epollop* epoll_op, int fd);
#if MTCP
int get_listen_sock(struct epollop* epoll_op, char* port);
#else
int get_listen_sock(char* port);
#endif
int init_event_io(int max, struct epollop* epoll_op);
int accept_connection(struct epollop* epoll_op, int fd);
void loop_run(struct epollop* epoll_op, int th_id);

#endif
