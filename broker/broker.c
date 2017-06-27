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

#include "broker.h"
#include <signal.h>
#ifdef DEBUG
#include <mcheck.h>
#endif
#include "utility.h"
#if MTCP
#include "mtcp_api.h"
#include "mtcp_epoll.h"
#include "cpu.h"
#endif

struct broker *g_broker;
int g_core_limit;
pthread_t g_tid[LOOP_COUNT];
uint32_t g_max_clients;

static void clean_context(struct client *context) {
    context->is_subscribe = false;
    context->sock = 0;
    context->num_sending_pack = 0;

    memset(context->id, 0, sizeof(context->id));
    context->last_mid = 0;
    context->state = cs_new;

    free_mqtt_msg(&(context->in_packet));
    memset(&(context->in_parser), 0, sizeof(mqtt_parser_t));
    free_mqtt_msg(&(context->out_packet));

    context->remaining_count = 0;
    context->remaining_mult = 0;
    if (context->in_payload) {
        free(context->in_payload);
        context->in_payload = NULL;
    }
    context->in_to_process = 0;
    context->in_cur = 0;
    
    if (context->sending_cur_packet) {
        free(context->sending_cur_packet);
        context->sending_cur_packet = NULL;
    }
}

void remove_client(struct epollop* ep_op, int fd) {
    struct client *context = NULL;

    context = &g_broker->clients[ep_op->base_sock + fd];
    HASH_DEL(ep_op->contexts_by_ptr, context);
    clean_context(context);
    disconnect_sock(ep_op, fd);
}

bool add_client(mqtt_buffer_t *id, struct epollop* ep_op, int fd) {
    struct client *client = NULL;

	client = &g_broker->clients[ep_op->base_sock + fd];
	/* used for hash_add_ptr in event_io.c */
	client->itself = client;
	memset(client->id, 0, sizeof(client->id));
	memcpy(client->id, id->data, id->length);

	g_broker->clients[ep_op->base_sock + fd].num_sending_pack = 0;
	g_broker->clients[ep_op->base_sock + fd].state = cs_connected;
	return true;
}

static void run(void *arg) {
    int th_id = *((int*)arg);
    
#if MTCP
    mtcp_core_affinitize(th_id);
    /* create mtcp context: this will spawn an mtcp thread */
    g_broker->ep_op[th_id].mctx = mtcp_create_context(th_id);
    if (!g_broker->ep_op[th_id].mctx) {
        write_errlog("Failed to create mtcp context!\n");
        return;
    }
#endif
    g_broker->ep_op[th_id].th_id = th_id;
    write_infolog("run in thread(%d)", th_id);
    if (init_event_io(MAX_EVENT_PER_LOOP, &g_broker->ep_op[th_id]) == -1) {
        write_errlog("error init event_io");
        return;
    }
#if MTCP
    if ((g_broker->ep_op[th_id].l_sock = get_listen_sock(&(g_broker->ep_op[th_id]), g_broker->port)) == -1) {
#else
    if ((g_broker->ep_op[th_id].l_sock = get_listen_sock(g_broker->port)) == -1) {
#endif
        write_errlog("error listen sock");
        return;
    }
    add_sock(&g_broker->ep_op[th_id], g_broker->ep_op[th_id].l_sock);
    loop_run(&g_broker->ep_op[th_id], th_id);

#if MTCP
    mtcp_destroy_context(g_broker->ep_op[th_id].mctx);
#endif
}

static int run_worker() {
    int i, arg[LOOP_COUNT];
    pthread_attr_t attr[LOOP_COUNT];

    int rc;
    for (i = 0; i < g_core_limit; i++) {
        usleep(10000);
        arg[i] = i;
        pthread_attr_init(&attr[i]);
        if ((rc = pthread_create(&(g_tid[i]), &attr[i], (void*(*)(void*))run, (void*)(arg + i))) != 0) {
            write_errlog("pthread_create %m");
            return rc;
        }
        size_t stacksize;
        pthread_attr_getstacksize(&attr[i], &stacksize);
    }

    int r = 0;
    for (i = 0; i < g_core_limit; i++) {
        if ((r = pthread_join(g_tid[i], NULL)) != 0)
            write_errlog("error on result of th join %d", r);
    }

    for (i = 0; i < g_core_limit; i++)
        pthread_attr_destroy(&attr[i]);

    write_infolog("pthread exit");
    return 0;
}

static int start_server(char *port) {
    g_broker->port = port;
    if (run_worker() == -1)
        return -1;
    return 0;
}

static int init_database() {
    struct subhier *child;

    if ((g_broker = (struct broker*)malloc(sizeof(struct broker))) == NULL) {
        write_errlog("Memory size is insufficient, required(%lu)", sizeof(struct broker));
        return -1;
    }
    memset(g_broker, 0, sizeof(struct broker));

    write_infolog("init client mutexs");
    
    g_broker->clients = malloc(sizeof(struct client) * g_max_clients);
    memset(g_broker->clients, 0, sizeof(struct client) * g_max_clients);
    for (int i = 0; i < g_max_clients; i++) {
        if (pthread_rwlock_init(&(g_broker->clients[i].sending_lock), NULL) != 0) {
            write_errlog("sending_lock init failed");
            return -1;
        }
        if (pthread_rwlock_init(&(g_broker->clients[i].self_lock), NULL) != 0) {
            write_errlog("self_lock init failed");
            return -1;
        }
    }
    write_infolog("finish client mutexs");

    child = malloc(sizeof(struct subhier));
    if (!child) {
	write_errlog("Out of memory.");
	return -1;
    }
    child->parent = NULL;
    child->next = NULL;
    child->topic = strdup("");//TODO free topic
    if (!child->topic) {
        write_errlog("Out of memory.");
        return -1;
    }
    child->subs = NULL;
    child->children = NULL;
    g_broker->subs.children = child;
    return 0;
}

#if MTCP
void SignalHandler(int signum) {
    int i;
    for (i = 0; i < g_core_limit; i++) {
        if (g_tid[i] == pthread_self()) {
            pthread_kill(g_tid[i], signum);
        }
    }
}

static int init_mTCP() {
    struct mtcp_conf mcfg;

    g_core_limit = GetNumCPUs();
    if (g_core_limit > LOOP_COUNT)
        g_core_limit = LOOP_COUNT;
    mtcp_getconf(&mcfg);
    mcfg.num_cores = g_core_limit;
    mtcp_setconf(&mcfg);
    int ret = mtcp_init("./broker.conf");
    if (ret) {
        write_errlog("Failed to initialize mtcp\n");
        return -1;
    }
    mtcp_register_signal(SIGINT, SignalHandler);
    return ret;
}
#endif

int main(int argc, char* argv[]) {
#ifdef DEBUG
    mtrace();
    write_debuglog("Debug mode");
#endif
    if (argc != 2) {
        write_errlog("Usage: [port]");
        return -1;
    }
    
#ifdef MTCP
    if (init_mTCP() != 0) {
        return -1;
    }
#else
    g_core_limit = LOOP_COUNT;
#endif
    g_max_clients = MAX_FD_PER_LOOP * g_core_limit;
    if (init_database() != 0) {
        return -1;
    }

    if (pthread_rwlock_init(&(g_broker->client_id_lock), NULL) != 0) {
        write_errlog("mutex global init failed");
        return -1;
    }

    start_server(argv[1]);

    pthread_rwlock_destroy(&(g_broker->client_id_lock));
#ifdef MTCP
    mtcp_destroy();
#endif
    return 0;
}
