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

#ifndef MQTT_H
#define MQTT_H

#include <stdint.h>
#include <string.h>

typedef enum mqtt_error_e {
    MQTT_ERROR_NONE = 0,
    MQTT_ERROR_PARSER_INVALID_STATE = 1,
    MQTT_ERROR_PARSER_INVALID_REMAINING_LENGTH = 2,
    MQTT_ERROR_PARSER_INVALID_MESSAGE_ID = 3,
    MQTT_ERROR_SERIALISER_INVALID_MESSAGE_ID = 4,
} mqtt_error_t;

typedef enum mqtt_type_e {
    MQTT_TYPE_CONNECT = 1,
    MQTT_TYPE_CONNACK = 2,
    MQTT_TYPE_PUBLISH = 3,
    /*MQTT_TYPE_PUBACK = 4,
    MQTT_TYPE_PUBREC = 5,
    MQTT_TYPE_PUBREL = 6,
    MQTT_TYPE_PUBCOMP = 7,*/
    MQTT_TYPE_SUBSCRIBE = 8,
    MQTT_TYPE_SUBACK = 9,
    MQTT_TYPE_UNSUBSCRIBE = 10,
    MQTT_TYPE_UNSUBACK = 11,
    MQTT_TYPE_PINGREQ = 12,
    MQTT_TYPE_PINGRESP = 13,
    MQTT_TYPE_DISCONNECT = 14,
} mqtt_type_t;

typedef enum mqtt_retain_e {
  MQTT_RETAIN_FALSE = 0,
  MQTT_RETAIN_TRUE = 1,
} mqtt_retain_t;

typedef enum mqtt_qos_e {
  MQTT_QOS_AT_MOST_ONCE = 0,
  MQTT_QOS_AT_LEAST_ONCE = 1,
  MQTT_QOS_EXACTLY_ONCE = 2,
} mqtt_qos_t;

typedef enum mqtt_dup_e {
  MQTT_DUP_FALSE = 0,
  MQTT_DUP_TRUE = 1,
} mqtt_dup_t;

typedef struct mqtt_buffer_s {
  uint32_t length;
  uint8_t* data;
} mqtt_buffer_t;

typedef struct mqtt_topic_s {
  struct mqtt_topic_s* next;
  mqtt_buffer_t name;
} mqtt_topic_t;

typedef struct mqtt_topicpair_s {
  struct mqtt_topicpair_s* next;
  mqtt_buffer_t name;
  mqtt_qos_t qos;
} mqtt_topicpair_t;

#define MQTT_MESSAGE_COMMON_FIELDS \
    mqtt_retain_t retain; \
    mqtt_qos_t qos; \
    mqtt_dup_t dup; \
    mqtt_type_t type; \
    uint32_t length;

typedef union mqtt_message_u {
  struct {
    MQTT_MESSAGE_COMMON_FIELDS
  } common;

  struct {
    MQTT_MESSAGE_COMMON_FIELDS

    mqtt_buffer_t protocol_name;
    uint8_t protocol_version;

    struct {
      char username_follows;
      char password_follows;
      char will_retain;
      char will_qos;
      char will;
      char clean_session;
    } flags;

    uint16_t keep_alive;

    mqtt_buffer_t client_id;

    mqtt_buffer_t will_topic;
    mqtt_buffer_t will_message;

    mqtt_buffer_t username;
    mqtt_buffer_t password;
  } connect;

  struct {
    MQTT_MESSAGE_COMMON_FIELDS

    uint8_t _unused;
    uint8_t return_code;
  } connack;

  struct {
    MQTT_MESSAGE_COMMON_FIELDS

    mqtt_buffer_t topic_name;
    uint16_t message_id;

    mqtt_buffer_t content;
  } publish;

  struct {
    MQTT_MESSAGE_COMMON_FIELDS

    uint16_t message_id;
  } puback;

  struct {
    MQTT_MESSAGE_COMMON_FIELDS

    uint16_t message_id;
  } pubrec;

  struct {
    MQTT_MESSAGE_COMMON_FIELDS

    uint16_t message_id;
  } pubrel;

  struct {
    MQTT_MESSAGE_COMMON_FIELDS

    uint16_t message_id;
  } pubcomp;

  struct {
    MQTT_MESSAGE_COMMON_FIELDS

    uint16_t message_id;
    mqtt_topicpair_t* topics;
  } subscribe;

  struct {
    MQTT_MESSAGE_COMMON_FIELDS

    uint16_t message_id;
    uint8_t return_code;
    //mqtt_topicpair_t* topics;
  } suback;

  struct {
    MQTT_MESSAGE_COMMON_FIELDS

    uint16_t message_id;
    mqtt_topic_t* topics;
  } unsubscribe;

  struct {
    MQTT_MESSAGE_COMMON_FIELDS

    uint16_t message_id;
  } unsuback;

  struct {
    MQTT_MESSAGE_COMMON_FIELDS
  } pingreq;

  struct {
    MQTT_MESSAGE_COMMON_FIELDS
  } pingresp;

  struct {
    MQTT_MESSAGE_COMMON_FIELDS
  } disconnect;
} mqtt_message_t;

typedef enum mqtt_parser_rc_e {
  MQTT_PARSER_RC_ERROR,
  MQTT_PARSER_RC_CONTINUE,
  MQTT_PARSER_RC_INCOMPLETE,
  MQTT_PARSER_RC_MORE_READ,
  MQTT_PARSER_RC_DONE,
  MQTT_PARSER_RC_WANT_MEMORY,
} mqtt_parser_rc_t;

typedef enum mqtt_parser_state_e {
    MQTT_PARSER_STATE_INITIAL,
    MQTT_PARSER_STATE_REMAINING_LENGTH,
    MQTT_PARSER_STATE_VARIABLE_HEADER,
    MQTT_PARSER_STATE_CONNECT,
    MQTT_PARSER_STATE_CONNECT_PROTOCOL_NAME,
    MQTT_PARSER_STATE_CONNECT_PROTOCOL_VERSION,
    MQTT_PARSER_STATE_CONNECT_FLAGS,
    MQTT_PARSER_STATE_CONNECT_KEEP_ALIVE,
    MQTT_PARSER_STATE_CONNECT_CLIENT_IDENTIFIER,
    MQTT_PARSER_STATE_CONNECT_WILL_TOPIC,
    MQTT_PARSER_STATE_CONNECT_WILL_MESSAGE,
    MQTT_PARSER_STATE_CONNECT_USERNAME,
    MQTT_PARSER_STATE_CONNECT_PASSWORD,
    MQTT_PARSER_STATE_CONNACK,
    MQTT_PARSER_STATE_PUBLISH,
    MQTT_PARSER_STATE_PUBLISH_TOPIC_NAME,
    MQTT_PARSER_STATE_PUBLISH_MESSAGE_ID,
    MQTT_PARSER_STATE_PUBLISH_MESSAGE,
    MQTT_PARSER_STATE_PUBACK,
    MQTT_PARSER_STATE_PUBREC,
    MQTT_PARSER_STATE_PUBREL,
    MQTT_PARSER_STATE_PUBCOMP,
    MQTT_PARSER_STATE_SUBSCRIBE,
    MQTT_PARSER_STATE_SUBSCRIBE_TOPIC_NAME,
    MQTT_PARSER_STATE_PINGREQ,
} mqtt_parser_state_t;

typedef struct mqtt_parser_s {
    mqtt_parser_rc_t rc;
    mqtt_parser_state_t state;
    mqtt_error_t error;
    char buffer_pending;
    uint8_t* buffer;
    size_t buffer_length;
    size_t pay_length;
} mqtt_parser_t;

typedef enum mqtt_serialiser_rc_e {
    MQTT_SERIALISER_RC_ERROR,
    MQTT_SERIALISER_RC_SUCCESS,
} mqtt_serialiser_rc_t;

void mqtt_parser_command(mqtt_message_t* message, uint8_t data);
size_t mqtt_serialiser_size(mqtt_message_t* message);
mqtt_serialiser_rc_t mqtt_serialiser_write(mqtt_message_t* message, uint8_t* buffer, size_t len);
void mqtt_message_dump(mqtt_message_t* message);
void mqtt_buffer_dump_ascii(mqtt_buffer_t* buffer);
void mqtt_buffer_dump_hex(mqtt_buffer_t* buffer);

mqtt_parser_rc_t mqtt_parser_execute(mqtt_parser_t* parser, mqtt_message_t* message, uint8_t* data, size_t len, size_t* nread);
void mqtt_parser_buffer(mqtt_parser_t* parser, uint8_t* buffer, size_t buffer_length); 
void mqtt_message_init(mqtt_message_t** message);
void free_mqtt_msg(mqtt_message_t **msg);

#endif
