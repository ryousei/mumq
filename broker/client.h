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

#ifndef CLIENT_H
#define CLIENT_H

#include <pthread.h>
#include "uthash.h"

#include "utility.h"
#include "mqtt.h"
#include "substree.h"

enum client_state {
    cs_new = 0,
    cs_connected = 1,
    cs_disconnecting = 2,
    cs_connect_async = 3,
    cs_connect_pending = 4,
    cs_connect_srv = 5,
    cs_disconnect_ws = 6,
    cs_disconnected = 7,
    cs_expiring = 8
};

struct client {
    int sock;
    int epoll_id;
    pthread_rwlock_t self_lock;

	/* [MQTT-3.1.3-5] at most 23 characters */
    char id[24];
    uint16_t last_mid;
    enum client_state state;

    mqtt_message_t *in_packet;
    mqtt_parser_t in_parser;
    mqtt_message_t *out_packet;

    long snd_time;
    uint32_t snd_num;
    struct packet *sending_packet;
    struct packet *sending_packet_tail;
    struct packet *sending_cur_packet;
    int num_sending_pack;

    pthread_rwlock_t sending_lock;

    int8_t remaining_count;
    uint32_t remaining_mult;
    uint8_t *in_payload;
    uint32_t in_to_process;
    uint32_t in_cur;

    bool is_subscribe;

    int msg_count;
    int msg_count12;

    struct subhier **subs;
    int sub_count;

    UT_hash_handle hh_id;
    UT_hash_handle hh;
    /* HASH_ADD_PTR requires a member of structure to be a key */
    struct client* itself;
};
#endif
