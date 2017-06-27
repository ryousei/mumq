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

#ifndef BROKER_H
#define BROKER_H

#define _LARGEFILE64_SOURCE
#include <stdio.h>
#include <pthread.h>
#include "uthash.h"

#include "config.h"
#include "event_io.h"
#include "client.h"
#include "mqtt.h"
#include "substree.h"

struct epollop;
struct client;

enum msg_direction {
    md_in = 0,
    md_out = 1
};

struct packet {
    uint8_t *send_buf;
    uint32_t wcur;
    uint32_t wlen;
    struct packet *next;
};

struct broker {
    struct client *contexts_by_id;
    pthread_rwlock_t client_id_lock;

    struct client *clients;

    struct subhier subs;

    struct epollop ep_op[LOOP_COUNT];

    char *port;
};

void remove_client(struct epollop *e_op, int fd);
bool add_client(mqtt_buffer_t *id, struct epollop* ep_op, int fd);

int loop_read_write(struct client* context, struct epollop* epfd, int fd, int events);

int handle_mqtt_command(struct epollop* e_op, struct client* context);

int mqtt3_sub_add(struct broker* bro, struct client *context, const char *sub, int qos, struct subhier *root);
int mqtt3_sub_tree_process(struct broker *bro, struct client* context, char *topic, uint8_t* payload, uint32_t len, void (*callback)(struct client*,char*,uint8_t*,uint32_t));
#endif
