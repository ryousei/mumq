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

#include <time.h>
#include "event_io.h"
#include "broker.h"
#include "utility.h"
#include "mqtt.h"
#if MTCP
#include "mtcp_api.h"
#endif

extern struct broker* g_broker;
int loop_read(struct client* context, struct epollop *ep_op, int fd, int events);

#ifndef MTCP
static void set_rcv_wrt_buffer(int fd) {
    int sock_buffer_size = SOCK_RECV_BUFF_SIZE;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sock_buffer_size, sizeof(int));
    sock_buffer_size = SOCK_SEND_BUFF_SIZE;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sock_buffer_size, sizeof(int));
}

static void set_nonblocking(int fd) {
    int flag = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
}
#endif

static int operate_epoll(struct epollop* epoll_op, int op, int fd, int events) {
#if MTCP
    struct mtcp_epoll_event m_epev;
    if (events != 0)
        m_epev.events = events;
    m_epev.data.sockid = fd;
    int ret = mtcp_epoll_ctl(epoll_op->mctx, epoll_op->epfd, op, fd, &m_epev);
    return ret;
#else
    struct epoll_event epev = {0, {0}};
    if (events != 0) {
        epev.events = events | EPOLLERR | EPOLLHUP;
        epev.data.fd = fd;
        return epoll_ctl(epoll_op->epfd, op, fd, &epev);
    } else {
        return epoll_ctl(epoll_op->epfd, op, fd, NULL);
    }
#endif
}

void modify_event(struct epollop* epoll_op, int fd, int events) {
#ifdef MTCP
    if (operate_epoll(epoll_op, MTCP_EPOLL_CTL_MOD, fd, events)) {
#else
    if (operate_epoll(epoll_op, EPOLL_CTL_MOD, fd, events)) {
#endif
        write_errlog("modify event %d %m", fd);
    }
}

void disconnect_sock(struct epollop* epoll_op, int fd) {
#if MTCP
    if (operate_epoll(epoll_op, MTCP_EPOLL_CTL_DEL, fd, 0) == -1) {
#else
    if (operate_epoll(epoll_op, EPOLL_CTL_DEL, fd, 0) == -1) {
#endif
        write_errlog("operate epoll %d %m", fd);
    }

#if MTCP
    int ret = mtcp_close(epoll_op->mctx, fd);
#else
    int ret = close(fd);
#endif
}

int add_sock(struct epollop* epoll_op, int fd) {
#if MTCP
    mtcp_setsock_nonblock(epoll_op->mctx, fd);
#else
    set_nonblocking(fd);
    set_rcv_wrt_buffer(fd);
#endif

#ifdef MTCP
    if (operate_epoll(epoll_op, MTCP_EPOLL_CTL_ADD, fd, MTCP_EPOLLIN) == -1) {
#else
    if (operate_epoll(epoll_op, EPOLL_CTL_ADD, fd, EPOLLIN) == -1) {
#endif
        write_errlog("operate epoll %m");
        return -1;
    }
    return 0;
}

#if MTCP
int get_listen_sock(struct epollop* epoll_op, char *port) {
#else
int get_listen_sock(char *port) {
    int optval;
#endif
    struct sockaddr_in addr;
    int fd;
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(atoi(port));
#if MTCP
    fd = mtcp_socket(epoll_op->mctx, AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        write_errlog("socket: %m");
        return -1;
    }
    int ret = mtcp_setsock_nonblock(epoll_op->mctx, fd);
    if (ret < 0) {
        write_errlog("non-block: %m");
        return -1;
    }
    if (mtcp_bind(epoll_op->mctx, fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
        write_errlog("Failed to bind to the listening socket!\n");
        return -1;
    }

    if (mtcp_listen(epoll_op->mctx, fd, ACCEPT_QUEUES) < 0) {
        write_errlog("listen(%d): %m", fd);
        close(fd);
        return -1;
    }
#else
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        write_errlog("socket: %m");
        return -1;
    }

    optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        write_errlog("bind(%d): %m", port);
        close(fd);
        return -1;
    }

    if (listen(fd, ACCEPT_QUEUES) < 0) {
        write_errlog("listen(%d): %m", fd);
        close(fd);
        return -1;
    }
#endif
    write_debuglog("listen sock(%d): %m", fd);
    return fd;
}

int init_event_io(int max, struct epollop* epoll_op) {
#if MTCP
    epoll_op->epfd = mtcp_epoll_create(epoll_op->mctx, MAX_EVENT_PER_LOOP);
    if (epoll_op->epfd < 0) {
        write_errlog("Failed to create epoll descriptor!");
        return -1;
    }
#else
    if ((epoll_op->epfd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
        write_errlog("epoll_create: %m");
        return -1;
    }
    fcntl(epoll_op->epfd, F_SETFD, FD_CLOEXEC);
#endif
    epoll_op->num_events = max;
#if MTCP
    epoll_op->fds = calloc(MAX_FD_PER_LOOP, sizeof(struct epollev));
    memset(epoll_op->fds, 0, sizeof(struct epollev)*MAX_FD_PER_LOOP);
#else
    epoll_op->fds = calloc(MAX_FD_PER_LOOP * LOOP_COUNT, sizeof(struct epollev));
    memset(epoll_op->fds, 0, sizeof(struct epollev)*MAX_FD_PER_LOOP*LOOP_COUNT);
#endif
    return epoll_op->epfd;
}

int accept_connection(struct epollop* epoll_op, int fd) {
    int sock;
    
#if MTCP
    if ((sock = mtcp_accept(epoll_op->mctx, fd, NULL, NULL)) <= 0) {
#else
    struct sockaddr saddr;
    socklen_t len = sizeof(struct sockaddr_in);
    if ((sock = accept(fd, (struct sockaddr *)&saddr, &len)) <= 0) {
#endif
        write_errlog("accept socket %m");
        return INVALID_SOCKET;
    }
#if MTCP
    if (sock >= MAX_FD_PER_LOOP) {
#else
    if (sock >= MAX_FD_PER_LOOP * LOOP_COUNT) {
#endif
        write_errlog("#fd exceeds %d", sock);
        close(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

void loop_run(struct epollop* epoll_op, int th_id) {
#if MTCP
    struct mtcp_epoll_event *pev = (struct mtcp_epoll_event*)malloc(sizeof(struct mtcp_epoll_event) * MAX_EVENT_PER_LOOP);
#else
    struct epoll_event *pev = (struct epoll_event*)malloc(sizeof(struct epoll_event) * MAX_EVENT_PER_LOOP);
#endif
    if (pev == NULL) {
        write_errlog("allocate epoll_event(%d): %m\n", MAX_EVENT_PER_LOOP);
        return;
    }
#if MTCP
    epoll_op->base_sock = th_id * MAX_FD_PER_LOOP;
#else
    epoll_op->base_sock = 0;
#endif
    while (1) {
        int i, ret;
        int timeout = 1;
#if MTCP
        ret = mtcp_epoll_wait(epoll_op->mctx, epoll_op->epfd, pev, epoll_op->num_events, timeout);
#else
        ret = epoll_wait(epoll_op->epfd, pev, epoll_op->num_events, timeout);
#endif
        if (ret == -1) {
            write_errlog("epoll[%d] run %m", th_id);
            break;
        }

        for (i = 0; i < ret; ++i) {
#if MTCP
            int fd = pev[i].data.sockid;
#else
            int fd = pev[i].data.fd;
#endif
            struct epollev *ev = &epoll_op->fds[fd];
            ev->events = pev[i].events;
            ev->thread_id = th_id;
            if (fd != epoll_op->l_sock) {
                if (loop_read(&g_broker->clients[epoll_op->base_sock + fd], epoll_op, fd, ev->events) <= 0) {
                    remove_client(epoll_op, fd);
                }
            } else {
                int sock = INVALID_SOCKET;
                if ((sock = accept_connection(epoll_op, fd)) != INVALID_SOCKET) {
                    g_broker->clients[epoll_op->base_sock + sock].sock = sock;
                    g_broker->clients[epoll_op->base_sock + sock].epoll_id = th_id;
                    int num_client = HASH_COUNT(epoll_op->contexts_by_ptr);
                    if (num_client < MAX_FD_PER_LOOP) {

                        add_sock(epoll_op, sock);

                        struct client* context = &(g_broker->clients[epoll_op->base_sock + sock]);
                        HASH_ADD_PTR(epoll_op->contexts_by_ptr, itself, context);
                    } else {
                        write_debuglog("epoll out of num[%d]", num_client);
                        remove_client(epoll_op, fd);
                    }
                }
            }
        }
        struct client *context = NULL;
        for (context = epoll_op->contexts_by_ptr; context != NULL; context = context->hh.next) {
            pthread_rwlock_rdlock(&(context->sending_lock));
            int num_sending = context->num_sending_pack;
            pthread_rwlock_unlock(&(context->sending_lock));

            if (num_sending < 1)
                continue;

            //set the sending limt per sec
            /*struct timespec spec;
            clock_gettime(CLOCK_REALTIME, &spec);
            //overlap under millisecond
            if (context->snd_time == spec.tv_sec) {
	        if (context->snd_num > PUB_RATE) {
                    write_errlog("wait over ratio %u", context->snd_num);
                    continue;
                }
            } else {
                context->snd_num = 0;
                context->snd_time = spec.tv_sec; 
            }
            if (context->snd_num + num_sending > PUB_RATE) {
                num_sending = PUB_RATE - context->snd_num;
            }*/

            while (num_sending-- > 0) {
                pthread_rwlock_wrlock(&context->sending_lock);
                if (context->sending_packet && !context->sending_cur_packet) {
                    context->sending_cur_packet = context->sending_packet;
                    context->sending_packet = context->sending_packet->next;
                    if (!context->sending_packet) {
                        context->sending_packet_tail = NULL;
                    }
                }
                --(context->num_sending_pack);
                pthread_rwlock_unlock(&context->sending_lock);

                uint32_t wlen = 0;
                bool is_skip = false;
                while (context->sending_cur_packet->wcur < context->sending_cur_packet->wlen) {
#if MTCP
                    if ((wlen = mtcp_write(epoll_op->mctx, context->sock, (char*)&(context->sending_cur_packet->send_buf[context->sending_cur_packet->wcur]), context->sending_cur_packet->wlen - context->sending_cur_packet->wcur)) <= 0) {
#else
                    if ((wlen = send(context->sock, &(context->sending_cur_packet->send_buf[context->sending_cur_packet->wcur]), context->sending_cur_packet->wlen - context->sending_cur_packet->wcur, MSG_NOSIGNAL)) <= 0) {
#endif
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            write_errlog("(%d,%d) write error (block) %m", th_id, context->sock);
                            is_skip = true;
                            break;
                        } else {
                            write_errlog("write critical error %m");
                            remove_client(epoll_op, context->sock);
                            is_skip = true;
                            break;
                        }
                    }
                    context->sending_cur_packet->wcur += wlen;
                }
                if (is_skip)
                    break;
                ++(context->snd_num);

                if (context->sending_cur_packet->wcur >= context->sending_cur_packet->wlen) {
                    if (context->sending_cur_packet->send_buf) {
                        free(context->sending_cur_packet->send_buf);
                    }
                    if (context->sending_cur_packet) {
                        free(context->sending_cur_packet);
                        context->sending_cur_packet = NULL;
                    }
                }
            }
        }
    }
    free(pev);
}

int loop_read(struct client* context, struct epollop *ep_op, int fd, int events) {
    uint8_t byte;
    int rlen = -1;

#ifndef MTCP
    if (events & EPOLLHUP) {
        write_errlog("(%d,%d) hung event %m", ep_op->th_id, fd);
        return -1;
    }
#endif

#if MTCP
    int err;
    socklen_t len = sizeof(err);
    if (events & MTCP_EPOLLERR) {
        if (mtcp_getsockopt(ep_op->mctx, fd, SOL_SOCKET, SO_ERROR, (void *)&err, &len) == 0) {
            if (err != ETIMEDOUT) {
                write_errlog("(%d,%d) Error event: %s", ep_op->th_id, fd, strerror(err));
            }
        }
#else    
    if (events & EPOLLERR) {
        write_errlog("(%d,%d) error event %m", ep_op->th_id, context->sock);
#endif
        return -1;
    }

#if MTCP
    if (events & MTCP_EPOLLIN) {
#else
    if (events & EPOLLIN) {
#endif
        /* emulate logic from mosquitto's */
        if (context->in_packet == NULL) {
            mqtt_message_init(&context->in_packet);
        }
        if (context->in_packet->common.type == 0) {
#if MTCP
            rlen = mtcp_recv(ep_op->mctx, fd, (char*)&byte, 1, 0);
#else
            rlen = recv(fd, &byte, 1, 0);
#endif
            if (rlen == 1) {
                mqtt_parser_command(context->in_packet, byte);
            } else {
                if (rlen == 0) {
                    write_infolog("(%d,%d) recv disconnected_1", ep_op->th_id, fd);
                    return 0; /* EOF */
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    //write_errlog("(%d) EAGAIN_1 on cmd %02x", rlen);
                    return 1;
                } else {
                    switch (errno) {
                        case ECONNRESET:
                            write_errlog("(%d,%d) reset on command %m", ep_op->th_id, fd);
                            return 0;
                        default:
                            write_errlog("(%d,%d) error on command %m", ep_op->th_id, fd);
                            return -3;
                    }
                }
            }
            context->remaining_mult = 1;
        }
        if (context->remaining_count <= 0) {
            do {
#if MTCP
                rlen = mtcp_recv(ep_op->mctx, fd, (char*)&byte, 1, 0);
#else
                rlen = recv(fd, &byte, 1, 0);
#endif
                if (rlen == 1) {
                    context->remaining_count--;
                    if (context->remaining_count < -4) {
                        write_errlog("protocol error");
                        return -1;
                    }

                    context->in_packet->common.length += (byte & 127) * context->remaining_mult;
                    context->remaining_mult *= 128;
                } else {
                    if (rlen == 0) {
                        write_infolog("(%d,%d) recv disconnected_2", ep_op->th_id, context->sock);
                        return 0;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        //write_errlog("(%d) EAGAIN_2 on length %m", context->sock);
                        return 1;
                    } else {
                        switch (errno) {
                            case ECONNRESET:
                                write_errlog("(%d,%d) reset on remaining length %m", ep_op->th_id, fd);
                                return 0;
                            default:
                                write_errlog("(%d,%d) error on remaining length %m", ep_op->th_id, fd);
                                return -3;
                        }
                    }
                }
            } while ((byte & 128) != 0);

            context->remaining_count *= -1;
            if (context->in_packet->common.length > 0) {
                context->in_payload = malloc(context->in_packet->common.length * sizeof(uint8_t));
                if (!context->in_payload) {
                    write_errlog("(%d) error malloc payload", context->sock); 
                    return -2;
                }
                context->in_to_process = context->in_packet->common.length;
            }
        }

        while (context->in_to_process > 0) {
#if MTCP
            rlen = mtcp_recv(ep_op->mctx, fd, (char*)&(context->in_payload[context->in_cur]), context->in_to_process, 0);
#else
            rlen = recv(fd, &(context->in_payload[context->in_cur]), context->in_to_process, 0);
#endif
            if (rlen > 0) {
                context->in_to_process -= rlen;
                context->in_cur += rlen;
            } else {
                if (rlen == 0) {
                    write_infolog("(%d,%d) recv disconnected_3", ep_op->th_id, context->sock);
                    return 0;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    //write_errlog("(%d) EAGAIN_3 on payload %m", fd);
                    return 1;
                } else {
                    switch (errno) {
                        case ECONNRESET:
                            write_errlog("(%d,%d) reset on payload %m", ep_op->th_id, context->sock);
                            return 0;
                        default:
                            write_errlog("(%d,%d) error on payload %m", ep_op->th_id, context->sock);
                            return -3;
                    }
                }
            }
        }
        context->in_cur = 0;
        context->remaining_count = 0;
        int rc = 0;
        size_t nread = 0;
        context->in_parser.state = MQTT_PARSER_STATE_INITIAL;
        do {
            rc = mqtt_parser_execute(&context->in_parser, context->in_packet, context->in_payload, context->in_packet->common.length, &nread);
            if (rc == MQTT_PARSER_RC_ERROR || rc == MQTT_PARSER_RC_INCOMPLETE) {
                write_errlog("(%d,%d) parsing err", ep_op->th_id, context->sock);
                return -1;
            }
        } while (rc == MQTT_PARSER_RC_CONTINUE || rc == MQTT_PARSER_RC_WANT_MEMORY);
        if ((rc = handle_mqtt_command(ep_op, context)) != 0) {
            return -1;
        }
        if (context->in_payload) {
            free(context->in_payload);
            context->in_payload = NULL;
        }
    }
    return rlen;
}
