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

#include "mqtt.h"
#include "broker.h"
#include "utility.h"

const char* mqtt_error_strings[] = {
  NULL,
  "[parser] left in an invalid state",
  "[parser] remaining length field was invalid",
  "[parser] message id was invalid",
  "[serialiser] message if was invalid",
};

const char* mqtt_error_string(mqtt_error_t error) {
  return mqtt_error_strings[error];
}

void mqtt_message_init(mqtt_message_t** message) {
    *message = (mqtt_message_t*)malloc(sizeof(mqtt_message_t));
    memset(*message, 0, sizeof(mqtt_message_t));
}

static void mqtt_buffer_dump(mqtt_buffer_t* buffer) {
  printf("[%d] ", buffer->length);

  char hex = 0;
  for (int i=0;i<buffer->length;++i) {
    if (buffer->data[i] < 0x20 || buffer->data[i] > 0x7e) {
      hex = 1;
      break;
    }
  }

  if (hex) {
    mqtt_buffer_dump_hex(buffer);
  } else {
    mqtt_buffer_dump_ascii(buffer);
  }
}

void mqtt_message_dump(mqtt_message_t* message) {
  printf("message\n");
  printf("  type:              %d\n", message->common.type);
  printf("  qos:               %d\n", message->common.qos);
  printf("  dup:               %s\n", message->common.dup    ? "true" : "false");
  printf("  retain:            %s\n", message->common.retain ? "true" : "false");

  if (message->common.type == MQTT_TYPE_CONNECT) {
    printf("  protocol name:     ");
    mqtt_buffer_dump(&(message->connect.protocol_name));
    printf("\n");

    printf("  protocol version:  %d\n", message->connect.protocol_version);

    printf("  has username:      %s\n", message->connect.flags.username_follows ? "true": "false");
    printf("  has password:      %s\n", message->connect.flags.password_follows ? "true": "false");
    printf("  has will:          %s\n", message->connect.flags.will ? "true": "false");
    printf("  will qos:          %d\n", message->connect.flags.will_qos);
    printf("  retains will:      %s\n", message->connect.flags.will_retain ? "true": "false");
    printf("  clean session:     %s\n", message->connect.flags.clean_session ? "true": "false");

    printf("  keep alive:        %d\n", message->connect.keep_alive);

    printf("  client id:         ");
    mqtt_buffer_dump(&(message->connect.client_id));
    printf("\n");

    printf("  will topic:        ");
    mqtt_buffer_dump(&(message->connect.will_topic));
    printf("\n");
    printf("  will message:      ");
    mqtt_buffer_dump(&(message->connect.will_message));
    printf("\n");

    printf("  username:          ");
    mqtt_buffer_dump(&(message->connect.username));
    printf("\n");
    printf("  password:          ");
    mqtt_buffer_dump(&(message->connect.password));
    printf("\n");
  }
}

void mqtt_buffer_dump_ascii(mqtt_buffer_t* buffer) {
  for (int i=0;i<buffer->length;++i) {
    printf("%c", buffer->data[i]);
  }
}

void mqtt_buffer_dump_hex(mqtt_buffer_t* buffer) {
  for (int i=0;i<buffer->length;++i) {
    printf("%02x ", buffer->data[i]);
  }
}

#define READ_STRING(info) { \
    if ((len - *nread) < 2) { \
		return MQTT_PARSER_RC_INCOMPLETE; \
    } \
    \
    int str_length = data[*nread] * 256 + data[*nread + 1]; \
    \
    if ((len - *nread - 2) < str_length) { \
		return MQTT_PARSER_RC_INCOMPLETE; \
    } \
    \
    info.length = str_length; \
    info.data = data + *nread + 2; \
    \
    *nread += 2 + str_length; \
}

#define READ_MESSAGE(info, str_len) { \
    if ((len - *nread) < 0) { \
    write_debuglog("READ MESSAGE (%d - %d) < 0", len, *nread); \
		return MQTT_PARSER_RC_INCOMPLETE; \
    } \
    \
    if ((len - *nread) < str_len) { \
    write_debuglog("READ MESSAGE (%d - %d) < %d", len, *nread, str_len); \
		return MQTT_PARSER_RC_INCOMPLETE; \
    } \
    \
    info.length = str_len; \
    info.data = data + *nread; \
    \
    *nread += str_len; \
}

mqtt_parser_rc_t mqtt_parser_execute(mqtt_parser_t* parser, mqtt_message_t* message, uint8_t* data, size_t len, size_t* nread) {
	do {
		switch (parser->state) {
			case MQTT_PARSER_STATE_INITIAL: {
				switch (message->common.type) {
					case MQTT_TYPE_CONNECT: {
						parser->state = MQTT_PARSER_STATE_CONNECT;
						break;
					}
					case MQTT_TYPE_CONNACK: {
						parser->state = MQTT_PARSER_STATE_CONNACK;
						break;
					}
					case MQTT_TYPE_PUBLISH: {
						parser->state = MQTT_PARSER_STATE_PUBLISH;
						break;
					}
					case MQTT_TYPE_SUBSCRIBE: {
						parser->state = MQTT_PARSER_STATE_SUBSCRIBE;
						break;
					}
					case MQTT_TYPE_PINGREQ: {
						parser->state = MQTT_PARSER_STATE_INITIAL;
						return MQTT_PARSER_RC_DONE;
					}
					case MQTT_TYPE_DISCONNECT: {
						parser->state = MQTT_PARSER_STATE_INITIAL;
						return MQTT_PARSER_RC_DONE;
					}
					default: {
						parser->error = MQTT_ERROR_PARSER_INVALID_MESSAGE_ID;
						write_debuglog("error undefined command!!!!");
						return MQTT_PARSER_RC_ERROR;
					}
				}
				break;
			}
			
			case MQTT_PARSER_STATE_VARIABLE_HEADER: {
				if (message->common.type == MQTT_TYPE_CONNECT) {
					parser->state = MQTT_PARSER_STATE_CONNECT_PROTOCOL_NAME;
				}
				break;
			}
			
			case MQTT_PARSER_STATE_CONNECT: {
				parser->state = MQTT_PARSER_STATE_CONNECT_PROTOCOL_NAME;
				break;
			}
			
			case MQTT_PARSER_STATE_CONNECT_PROTOCOL_NAME: {
				READ_STRING(message->connect.protocol_name)
				parser->state = MQTT_PARSER_STATE_CONNECT_PROTOCOL_VERSION;
				break;
			}
			
			case MQTT_PARSER_STATE_CONNECT_PROTOCOL_VERSION: {
				if ((len - *nread) < 1) {
					return MQTT_PARSER_RC_INCOMPLETE;
				}
				
				message->connect.protocol_version = data[*nread];
				*nread += 1;
				
				parser->state = MQTT_PARSER_STATE_CONNECT_FLAGS;
				break;
			}
			
			case MQTT_PARSER_STATE_CONNECT_FLAGS: {
				if ((len - *nread) < 1) {
					return MQTT_PARSER_RC_INCOMPLETE;
				}

				message->connect.flags.username_follows = (data[*nread] >> 7) & 0x01;
				message->connect.flags.password_follows = (data[*nread] >> 6) & 0x01;
				message->connect.flags.will_retain      = (data[*nread] >> 5) & 0x01;
				message->connect.flags.will_qos         = (data[*nread] >> 4) & 0x02;
				message->connect.flags.will             = (data[*nread] >> 2) & 0x01;
				message->connect.flags.clean_session    = (data[*nread] >> 1) & 0x01;
				*nread += 1;

				parser->state = MQTT_PARSER_STATE_CONNECT_KEEP_ALIVE;
				break;
			}
			
			case MQTT_PARSER_STATE_CONNECT_KEEP_ALIVE: {
				if ((len - *nread) < 2) {
					return MQTT_PARSER_RC_INCOMPLETE;
				}

				message->connect.keep_alive = (data[*nread] << 8) + data[*nread + 1];
				*nread += 2;

				parser->state = MQTT_PARSER_STATE_CONNECT_CLIENT_IDENTIFIER;
				break;
			}
			
			case MQTT_PARSER_STATE_CONNECT_CLIENT_IDENTIFIER: {
				READ_STRING(message->connect.client_id)

				parser->state = MQTT_PARSER_STATE_CONNECT_WILL_TOPIC;
				break;
			}
			
			case MQTT_PARSER_STATE_CONNECT_WILL_TOPIC: {
				if (message->connect.flags.will) {
					READ_STRING(message->connect.will_topic)
				}

				parser->state = MQTT_PARSER_STATE_CONNECT_WILL_MESSAGE;
				break;
			}
			
			case MQTT_PARSER_STATE_CONNECT_WILL_MESSAGE: {
				if (message->connect.flags.will) {
					READ_STRING(message->connect.will_message)
				}

				parser->state = MQTT_PARSER_STATE_CONNECT_USERNAME;
				break;
			}
			
			case MQTT_PARSER_STATE_CONNECT_USERNAME: {
				if (message->connect.flags.username_follows) {
					READ_STRING(message->connect.username)
				}

				parser->state = MQTT_PARSER_STATE_CONNECT_PASSWORD;
				break;
			}

			case MQTT_PARSER_STATE_CONNECT_PASSWORD: {
				if (message->connect.flags.password_follows) {
					READ_STRING(message->connect.password)
				}

				parser->state = MQTT_PARSER_STATE_INITIAL;
				return MQTT_PARSER_RC_DONE;
			}

			case MQTT_PARSER_STATE_CONNACK: {
				if ((len - *nread) < 2) {
					return MQTT_PARSER_RC_INCOMPLETE;
				}

				message->connack._unused     = data[*nread];
				message->connack.return_code = data[*nread + 1];
				*nread += 2;

				parser->state = MQTT_PARSER_STATE_INITIAL;
				return MQTT_PARSER_RC_DONE;
			}
			
			case MQTT_PARSER_STATE_PUBLISH: {
				parser->state = MQTT_PARSER_STATE_PUBLISH_TOPIC_NAME;
				break;
			}
			
			case MQTT_PARSER_STATE_PUBLISH_TOPIC_NAME: {
				READ_STRING(message->publish.topic_name)
				
				parser->state = MQTT_PARSER_STATE_PUBLISH_MESSAGE_ID;
				break;
			}
			
			case MQTT_PARSER_STATE_PUBLISH_MESSAGE_ID: {
				if ((len - *nread) < 2) {
					return MQTT_PARSER_RC_INCOMPLETE;
				}

				message->publish.message_id = 0;
				if (message->common.qos > 0 && message->common.qos < 3) {
					message->publish.message_id = (data[*nread] << 8) + data[*nread + 1];
					*nread += 2;
				}

				parser->state = MQTT_PARSER_STATE_PUBLISH_MESSAGE;
				break;
			}
			
			case MQTT_PARSER_STATE_PUBLISH_MESSAGE: {
				uint32_t msg_len = message->common.length - message->publish.topic_name.length - 2;

				if (message->common.qos > 0 && message->common.qos < 3) {
					msg_len -= 2;
				}

				READ_MESSAGE(message->publish.content, msg_len)

				parser->state = MQTT_PARSER_STATE_INITIAL;
				return MQTT_PARSER_RC_DONE;
			}

			case MQTT_PARSER_STATE_SUBSCRIBE: {
				if ((len - *nread) < 2) {
					return MQTT_PARSER_RC_INCOMPLETE;
				}

				message->subscribe.message_id = (data[*nread] << 8) + data[*nread + 1];
				*nread += 2;

				parser->state = MQTT_PARSER_STATE_SUBSCRIBE_TOPIC_NAME;
				break;
			}
			
			case MQTT_PARSER_STATE_SUBSCRIBE_TOPIC_NAME: {
				if (message->subscribe.topics == NULL) {
					message->subscribe.topics = malloc(sizeof(mqtt_topicpair_t));
					message->subscribe.topics->next = NULL;
				}
				
				READ_STRING(message->subscribe.topics->name)
				
				message->subscribe.topics->qos = data[*nread];
				*nread += 1;

				parser->state = MQTT_PARSER_STATE_INITIAL;
				return MQTT_PARSER_RC_DONE;
			}
			
			default: {
				parser->error = MQTT_ERROR_PARSER_INVALID_STATE;
				return MQTT_PARSER_RC_ERROR;
			}
		}
	} while (1);
}

void mqtt_parser_command(mqtt_message_t* message, uint8_t data) {
    message->common.retain = (data >> 0) & 0x01;
    message->common.qos    = (data >> 1) & 0x03;
    message->common.dup    = (data >> 3) & 0x01;
    message->common.type   = (data >> 4) & 0x0f;
}

#define WRITE_STRING(name) { \
	buffer[offset++] = name.length / 0xff; \
	buffer[offset++] = name.length & 0xff; \
	memcpy(&(buffer[offset]), name.data, name.length); \
	offset += name.length; \
}

size_t mqtt_serialiser_size(mqtt_message_t* message) {
	size_t len = 1;

    if (message->common.length <= 127) {
        len += 1;
    } else if (message->common.length <= 16383) {
        len += 2;
    } else if (message->common.length <= 2097151) {
        len += 3;
    } else if (message->common.length <= 268435455) {
        len += 4;
    }

    if (message->common.type == MQTT_TYPE_PUBLISH) {
        //only qos 0
        len += 2;
        len += message->publish.topic_name.length;
        len += message->publish.content.length;
    } else if (message->common.type == MQTT_TYPE_SUBACK) {
        len += 3;
    } else if (message->common.type == MQTT_TYPE_CONNECT) {
        len += 8;

        len += message->connect.protocol_name.length;
        len += message->connect.client_id.length;

        if (message->connect.flags.username_follows) {
            len += 2;
            len += message->connect.username.length;
        }

        if (message->connect.flags.password_follows) {
            len += 2;
            len += message->connect.password.length;
        }

        if (message->connect.flags.will) {
            len += 4;
            len += message->connect.will_topic.length;
            len += message->connect.will_message.length;
        }
    } else if (message->common.type == MQTT_TYPE_CONNACK) {
        len += 2;
    }

  return len;
}

mqtt_serialiser_rc_t mqtt_serialiser_write(mqtt_message_t* message, uint8_t* buffer, size_t len) {
    unsigned int offset = 0;

    buffer[offset++] = message->common.retain + (message->common.qos << 1) + (message->common.dup << 3) + (message->common.type << 4);

    uint32_t remaining_length = message->common.length;

    do {
        buffer[offset] = remaining_length % 128;
        remaining_length /= 128;
        if (remaining_length > 0)
            buffer[offset] |= 0x80;
        offset++;
    } while (remaining_length > 0);

    switch (message->common.type) {
        case MQTT_TYPE_PUBLISH: {
            WRITE_STRING(message->publish.topic_name);
            memcpy(&(buffer[offset]), message->publish.content.data, message->publish.content.length);
            break;
        }
        case MQTT_TYPE_SUBACK: {
            buffer[offset++] = message->suback.message_id >> 8;
            buffer[offset++] = message->suback.message_id & 0xff;
            buffer[offset++] = message->suback.return_code;
            break;
        }
        case MQTT_TYPE_CONNECT: {
            WRITE_STRING(message->connect.protocol_name);

            buffer[offset++] = message->connect.protocol_version;

            buffer[offset++] =
                (message->connect.flags.username_follows << 7) +
                (message->connect.flags.password_follows << 6) +
                (message->connect.flags.will_retain      << 5) +
                (message->connect.flags.will_qos         << 3) +
                (message->connect.flags.will             << 2) +
                (message->connect.flags.clean_session    << 1);

                buffer[offset++] = message->connect.keep_alive >> 8;
                buffer[offset++] = message->connect.keep_alive & 0xff;

                WRITE_STRING(message->connect.client_id);

            if (message->connect.flags.will) {
                WRITE_STRING(message->connect.will_topic);
                WRITE_STRING(message->connect.will_message);
            }

            if (message->connect.flags.username_follows) {
                WRITE_STRING(message->connect.username);
            }

            if (message->connect.flags.password_follows) {
                WRITE_STRING(message->connect.password);
            }

            break;
        }

        case MQTT_TYPE_CONNACK: {
            buffer[offset++] = message->connack._unused;
            buffer[offset++] = message->connack.return_code;

            break;
        }

        default: {
            return MQTT_SERIALISER_RC_ERROR;
        }
    }

    return MQTT_SERIALISER_RC_SUCCESS;
}

#define FREE_BUFFER(name) { \
  if (name.data) {    \
      free(name.data);  \
      name.data = NULL; \
  } \
}

void free_mqtt_msg(mqtt_message_t **msg) {
    if (*msg) {
        switch ((*msg)->common.type) {
            case MQTT_TYPE_SUBSCRIBE: {
                free((*msg)->subscribe.topics);
                break;
            }
            default:
                break;
        }
        free(*msg);
        *msg = NULL;
    }
}
