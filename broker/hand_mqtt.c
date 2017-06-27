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

#include <sys/types.h>
#ifndef MTCP
#include <sys/socket.h>
#endif
#include "broker.h"
#include "utility.h"

extern struct broker *g_broker;

static void publish_msg(struct client* context, char* topic, uint8_t* payload, uint32_t payload_len) {
    /* this function is called by threads handling publishers, meaning it is not thread safe */
    int topic_len, packet_len; mqtt_message_t out_packet;
    struct packet *new_packet;
    
    memset(&out_packet, 0, sizeof(mqtt_message_t));
    out_packet.publish.type = MQTT_TYPE_PUBLISH;
    topic_len = strlen(topic);
    packet_len = 2 + topic_len + payload_len;
    out_packet.publish.length = packet_len;
    out_packet.publish.topic_name.data = (uint8_t*)topic;
    out_packet.publish.topic_name.length = topic_len;
    out_packet.publish.content.data = payload;
    out_packet.publish.content.length = payload_len;

    new_packet = malloc(sizeof(struct packet));
    new_packet->wlen = mqtt_serialiser_size(&out_packet);
    new_packet->send_buf = malloc(new_packet->wlen);
    mqtt_serialiser_write(&out_packet, new_packet->send_buf, new_packet->wlen);
    new_packet->wcur = 0;
    new_packet->next = NULL;
    
    pthread_rwlock_wrlock(&(context->sending_lock));
    if (context->sending_packet_tail) {
        context->sending_packet_tail->next = new_packet;
    } else {
        context->sending_packet = new_packet;
    }
    context->sending_packet_tail = new_packet;
    ++(context->num_sending_pack);
    pthread_rwlock_unlock(&(context->sending_lock));
}

static void ack_msg(struct client* context, enum mqtt_type_e mqtt_cmd, uint32_t payload_len, uint8_t ret_code) {
    mqtt_message_t out_packet;
    struct packet *new_packet;
    
    memset(&out_packet, 0, sizeof(mqtt_message_t));
    switch (mqtt_cmd) {
        case MQTT_TYPE_CONNACK:
            out_packet.connack.type = MQTT_TYPE_CONNACK;
            out_packet.connack.length = payload_len;
            out_packet.connack._unused = 0;
            out_packet.connack.return_code = ret_code;
        break;
        case MQTT_TYPE_SUBACK:
            out_packet.suback.type = MQTT_TYPE_SUBACK;
            out_packet.suback.length = payload_len;
            out_packet.suback.message_id = context->last_mid;
            out_packet.suback.return_code = ret_code;
        break;
        case MQTT_TYPE_PINGRESP:
            out_packet.pingresp.type = MQTT_TYPE_PINGRESP;
            out_packet.pingresp.length = payload_len;
        break;
        default:
            write_errlog("not support command %d", mqtt_cmd);
        return;
    }
    
    new_packet = malloc(sizeof(struct packet));
    new_packet->wlen = mqtt_serialiser_size(&out_packet);
    new_packet->send_buf = malloc(new_packet->wlen);
    mqtt_serialiser_write(&out_packet, new_packet->send_buf, new_packet->wlen);
    new_packet->wcur = 0;
    new_packet->next = NULL;
    
    pthread_rwlock_wrlock(&(context->sending_lock));
    if (context->sending_packet_tail) {
        context->sending_packet_tail->next = new_packet;
    } else {
        context->sending_packet = new_packet;
    }
    context->sending_packet_tail = new_packet;
    ++(context->num_sending_pack);
    pthread_rwlock_unlock(&(context->sending_lock));
}

int handle_mqtt_command(struct epollop* e_op, struct client* context) {
    uint8_t ret_code = 0;/* CONNACK_ACCEPTED */
    mqtt_message_t *in_msg = context->in_packet;
    switch (in_msg->common.type) {
        case MQTT_TYPE_PUBLISH: {
            if (in_msg->common.qos == 0) {
                if (in_msg->publish.topic_name.data != NULL) {
                    if (!pub_topic_valid((char*)in_msg->publish.topic_name.data)) {
                        ;//TODO disconnect or sent error code ?
                    }
                    if (context != NULL) {
                        uint8_t *content = malloc(in_msg->publish.content.length+1);
                        memcpy(content, in_msg->publish.content.data, in_msg->publish.content.length);
                        content[in_msg->publish.content.length] = 0;

                        char *topic = malloc(in_msg->publish.topic_name.length+1);
                        memcpy(topic, in_msg->publish.topic_name.data, in_msg->publish.topic_name.length);
                        topic[in_msg->publish.topic_name.length] = 0;
                        context->last_mid = in_msg->publish.message_id;

                        mqtt3_sub_tree_process(g_broker, context, topic, content, in_msg->publish.content.length, publish_msg);
                        free(topic);
                        free(content);
                    }
                }
            } else {
                //FUTURE: qos 1,2 -> ack to a sender
            }
        }
        break;
        case MQTT_TYPE_CONNECT: {
            if (in_msg->connect.client_id.length > 23) {
                write_infolog("client id length exceeds");
                ret_code = 2;
            } else if (in_msg->connect.client_id.length > 0) {
                if (add_client(&(in_msg->connect.client_id), e_op, context->sock) == 1) {
                    ;
                } else {
                    write_infolog("%s already exists", context->id);
                    ret_code = 2;
                }
            }
            ack_msg(context, MQTT_TYPE_CONNACK, 2, ret_code);
        }
        break;
        case MQTT_TYPE_SUBSCRIBE: {
            mqtt_topicpair_t *topic_pair = in_msg->subscribe.topics;
            //FUTURE: support more one topic per subscribing request
            if (topic_pair != NULL) {
                if (topic_pair->name.data != NULL) {
                    if (topic_pair->name.length < 0) {
                        ;//TODO disconnect or sent error code ?
                    }
                    if (!sub_topic_valid((char*)topic_pair->name.data)) {
                        ;//TODO disconnect or sent error code ?
                    }
                    if (context->state == cs_connected) {
                        char *topic = malloc(topic_pair->name.length+1);
                        memcpy(topic, topic_pair->name.data, topic_pair->name.length);
                        topic[topic_pair->name.length] = 0;
                        context->last_mid = in_msg->subscribe.message_id;
                        if ((ret_code = mqtt3_sub_add(g_broker, context, topic, topic_pair->qos, &g_broker->subs)) == 0) {
                            context->is_subscribe = true;
                        }
                        //mqtt3_sub_tree_print(&g_broker->subs, 0);
                        free(topic);
                    } else {
                        ret_code = 0x80;
                    }
                }
            }
            ack_msg(context, MQTT_TYPE_SUBACK, 3, ret_code);
        }
        break;
        case MQTT_TYPE_PINGREQ:
            ack_msg(context, MQTT_TYPE_PINGRESP, 0, ret_code);
        break;
        case MQTT_TYPE_UNSUBSCRIBE:
            break;
        case MQTT_TYPE_DISCONNECT:
            write_infolog("disconnect %s", context->id);
            ret_code = 1;
        break;
        default:
            write_errlog("not support command %d", in_msg->common.type);
        break;
    }
    free_mqtt_msg(&context->in_packet);
    return ret_code;
}
