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

#ifndef SUBSTREE_H
#define SUBSTREE_H

#include <stdint.h>

struct msg_store {
	struct msg_store *next;
	struct msg_store *prev;

	char *source_id;
	char **dest_ids;
	int dest_id_count;
	int ref_count;
	char *topic;
	void *payload;
	uint32_t payloadlen;
	uint16_t source_mid;
	uint16_t mid;
	uint8_t qos;
	bool retain;
};

struct subleaf {
	struct subleaf *prev;
	struct subleaf *next;
	struct client *context;
	int qos;
};

struct subhier {
	struct subhier *parent;
	struct subhier *children;
	struct subhier *next;
	struct subleaf *subs;
	char *topic;
	struct msg_store *retained;
};

void mqtt3_sub_tree_print(struct subhier *root, int level);

#endif