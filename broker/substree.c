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

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "broker.h"
#include "utility.h"

pthread_rwlock_t branch_lock = PTHREAD_RWLOCK_INITIALIZER;

struct _sub_token {
	struct _sub_token *next;
	char *topic;
};

static struct _sub_token* _sub_topic_append(struct _sub_token **tail, struct _sub_token **topics, char *topic) {
	struct _sub_token *new_topic;

	if (!topic) {
		return NULL;
	}
	new_topic = malloc(sizeof(struct _sub_token));
	if (!new_topic) {
		free(topic);
		return NULL;
	}
	new_topic->next = NULL;
	new_topic->topic = topic;

	if (*tail) {
		(*tail)->next = new_topic;
		*tail = (*tail)->next;
	} else {
		*topics = new_topic;
		*tail = new_topic;
	}
	return new_topic;
}

static int _sub_topic_tokenise(const char *subtopic, struct _sub_token **topics) {
	struct _sub_token *new_topic, *tail = NULL;
	int len;
	int start, stop, tlen;
	int i;
	char *topic;

	if (subtopic[0] != '$') {
		new_topic = _sub_topic_append(&tail, topics, strdup(""));
		if (!new_topic)
            goto cleanup;
	}

	len = strlen(subtopic);

	if (subtopic[0] == '/') {
		new_topic = _sub_topic_append(&tail, topics, strdup(""));
		if(!new_topic)
            goto cleanup;
		start = 1;
	} else {
		start = 0;
	}

	stop = 0;
	for (i = start; i < len+1; i++) {
		if (subtopic[i] == '/' || subtopic[i] == '\0') {
			stop = i;
			if (start != stop) {
				tlen = stop - start;

				topic = malloc(tlen+1);
				if(!topic)
                    goto cleanup;
				memcpy(topic, &subtopic[start], tlen);
				topic[tlen] = '\0';
			} else {
				topic = strdup("");
				if(!topic)
                    goto cleanup;
			}
			new_topic = _sub_topic_append(&tail, topics, topic);
			if(!new_topic)
                goto cleanup;
			start = i+1;
		}
	}

	return 0;

cleanup:
	tail = *topics;
	*topics = NULL;
	while (tail) {
		if (tail->topic)
            free(tail->topic);
		new_topic = tail->next;
		free(tail);
		tail = new_topic;
	}
	return 1;
}

static void _sub_topic_tokens_free(struct _sub_token *tokens) {
	struct _sub_token *tail;

	while (tokens) {
		tail = tokens->next;
		if (tokens->topic) {
			free(tokens->topic);
		}
		free(tokens);
		tokens = tail;
	}
}

static int _sub_add(struct broker *bro, struct client *context, int qos, struct subhier *subhier, struct _sub_token *tokens) {
	struct subhier *branch, *last = NULL;
	struct subleaf *leaf, *last_leaf;
	struct subhier **subs;
	int i;

	if (!tokens) {
		if (context && context->id) {
			leaf = subhier->subs;
			last_leaf = NULL;
			while (leaf) {
				if (leaf->context && leaf->context->id && !strcmp(leaf->context->id, context->id)) {
					leaf->qos = qos;
                    return 0;
				}
				last_leaf = leaf;
				leaf = leaf->next;
			}
			leaf = malloc(sizeof(struct subleaf));
			if (!leaf)
                return -2;//No memory
			leaf->next = NULL;
			leaf->context = context;
			leaf->qos = qos;
			for (i = 0; i < context->sub_count; i++) {
				if (!context->subs[i]) {
					context->subs[i] = subhier;
					break;
				}
			}
			if (i == context->sub_count) {
				subs = realloc(context->subs, sizeof(struct subhier *)*(context->sub_count + 1));
				if (!subs) {
					free(leaf);
					return -2;
				}
				context->subs = subs;
				context->sub_count++;
				context->subs[context->sub_count-1] = subhier;
			}
			if (last_leaf) {
				last_leaf->next = leaf;
				leaf->prev = last_leaf;
			} else {
				subhier->subs = leaf;
				leaf->prev = NULL;
			}
		}
		return 0;
	}

	branch = subhier->children;
	while (branch) {
		if (!strcmp(branch->topic, tokens->topic)) {
			return _sub_add(bro, context, qos, branch, tokens->next);
		}
		last = branch;
		branch = branch->next;
	}
	/* Not found */
	branch = calloc(1, sizeof(struct subhier));
	if (!branch)
        return -2;
	branch->parent = subhier;
	branch->topic = strdup(tokens->topic);
	if (!branch->topic) {
		free(branch);
		return -2;
	}
	if (!last) {
        pthread_rwlock_wrlock(&branch_lock);
		subhier->children = branch;
        pthread_rwlock_unlock(&branch_lock);
	} else {
		last->next = branch;
	}
	return _sub_add(bro, context, qos, branch, tokens->next);
}

int mqtt3_sub_add(struct broker* bro, struct client *context, const char *sub, int qos, struct subhier *root) {
	int rc = 0;
	struct subhier *subhier, *child;
	struct _sub_token *tokens = NULL;
    static pthread_rwlock_t root_lock = PTHREAD_RWLOCK_INITIALIZER;
    
	if (_sub_topic_tokenise(sub, &tokens))
        return 1;

    pthread_rwlock_wrlock(&root_lock);

	subhier = root->children;
	while (subhier) {
		if (!strcmp(subhier->topic, tokens->topic)) {
			rc = _sub_add(bro, context, qos, subhier, tokens);
			break;
		}
		subhier = subhier->next;
	}
	if (!subhier) {
		child = malloc(sizeof(struct subhier));
		if (!child) {
			_sub_topic_tokens_free(tokens);
			return -2;
		}
		child->parent = root;
		child->topic = strdup(tokens->topic);
		if (!child->topic) {
			_sub_topic_tokens_free(tokens);
			free(child);
			return -2;
		}
		child->subs = NULL;
		child->children = NULL;
		if (bro->subs.children) {
			child->next = bro->subs.children;
		} else {
			child->next = NULL;
		}
		bro->subs.children = child;

		rc = _sub_add(bro, context, qos, child, tokens);
	}

	pthread_rwlock_unlock(&root_lock);

	_sub_topic_tokens_free(tokens);

	if(rc == -1)
        rc = 0;
	return rc;
}

static int _subs_process(struct subhier *hier, struct client* context, char *topic, uint8_t* payload, uint32_t len, void(*callback)(struct client*,char*,uint8_t*,uint32_t)) {
	int rc = 0;
	struct subleaf *leaf;

	leaf = hier->subs;
	while (context->id && leaf) {
		if (!leaf->context->id) {
			leaf = leaf->next;
			continue;
		}
        if (leaf->context->is_subscribe == true) {
            callback(leaf->context, topic, payload, len);
        }
        rc = 1;
		leaf = leaf->next;
	}
	return rc;
}

static void _sub_search(struct subhier *subhier, struct _sub_token *tokens, struct client* context, char *topic, uint8_t* payload, uint32_t len, void(*callback)(struct client*,char*,uint8_t*,uint32_t)) {

	struct subhier *branch;
	
	pthread_rwlock_rdlock(&branch_lock);
	branch = subhier->children;
	pthread_rwlock_unlock(&branch_lock);
	while (branch) {
		if (tokens && tokens->topic && (!strcmp(branch->topic, tokens->topic) || !strcmp(branch->topic, "+"))) {
			_sub_search(branch, tokens->next, context, topic, payload, len, callback);
			if (!tokens->next) {
				_subs_process(branch, context, topic, payload, len, callback);
			}
		} else if (!strcmp(branch->topic, "#") && !branch->children) {
			_subs_process(branch, context, topic, payload, len, callback);
		}
		branch = branch->next;
	}
}

int mqtt3_sub_tree_process(struct broker *db, struct client* context, char *topic, uint8_t* payload, uint32_t len, void(*callback)(struct client*,char*,uint8_t*,uint32_t)) {
    int rc = 0;
	struct subhier *subhier;
	struct _sub_token *tokens = NULL;

	if(_sub_topic_tokenise(topic, &tokens))
        return 1;

	subhier = db->subs.children;
	while (subhier) {
		if (!strcmp(subhier->topic, tokens->topic)) {
			_sub_search(subhier, tokens, context, topic, payload, len, callback);
		}
		subhier = subhier->next;
	}
	_sub_topic_tokens_free(tokens);

	return rc;
}

void mqtt3_sub_tree_print(struct subhier *root, int level) {
	int i;
	struct subhier *branch;
	struct subleaf *leaf;

	for (i=0; i < level*2; i++) {
		printf(" ");
	}
	printf("%s", root->topic);
	leaf = root->subs;
	while (leaf) {
		if (leaf->context) {
			printf(" (%s, %d)", leaf->context->id, leaf->qos);
		} else {
			printf(" (%s, %d)", "", leaf->qos);
		}
		leaf = leaf->next;
	}
	if (root->retained) {
		printf(" (r)");
	}
	printf("\n");

	branch = root->children;
	while (branch) {
		mqtt3_sub_tree_print(branch, level+1);
		branch = branch->next;
	}
}
