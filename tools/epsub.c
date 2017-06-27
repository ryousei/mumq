#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>
#include <assert.h>
#include <sys/epoll.h>

#include "mosquitto.h"
#include "debug.h"
#include "cpu.h"

#define MAX_CPUS 16

#define MAX_URL_LEN 128
#define MAX_FILE_LEN 128
#define HTTP_HEADER_LEN 1024

#define IP_RANGE 1
#define MAX_IP_STR_LEN 16

#define BUF_SIZE (8*1024)

#define CALC_MD5SUM FALSE

#define TIMEVAL_TO_MSEC(t)		((t.tv_sec * 1000) + (t.tv_usec / 1000))
#define TIMEVAL_TO_USEC(t)		((t.tv_sec * 1000000) + (t.tv_usec))
#define TS_GT(a,b)			((int64_t)((a)-(b)) > 0)

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#ifndef ERROR
#define ERROR (-1)
#endif

/*----------------------------------------------------------------------------*/
static pthread_t app_thread[MAX_CPUS];
static int done[MAX_CPUS];
/*----------------------------------------------------------------------------*/
static int num_cores;
static int core_limit;
/*----------------------------------------------------------------------------*/
static char* topic;
static int size;
static char* message;
static bool is_unique;
static char* client_id;
/*----------------------------------------------------------------------------*/
static int fio = FALSE;
static char outfile[MAX_FILE_LEN + 1];
/*----------------------------------------------------------------------------*/
static char host[MAX_IP_STR_LEN + 1];
static in_addr_t daddr;
static in_port_t dport;
/*----------------------------------------------------------------------------*/
static uint64_t flows_per_con;
static uint64_t total_flows;
static uint64_t total_concurrency;
static int concurs[MAX_CPUS];
static int concurrency;
static int max_fds;
static struct timeval exec_stv = {0};
static struct timeval exec_etv;
/*----------------------------------------------------------------------------*/
struct sub_stat
{
	uint64_t waits;
	uint64_t events;
	uint64_t connects;
	uint64_t rcv_msgs;
	uint64_t completes;

	uint64_t errors;

	uint64_t sum_rcv_time;
	uint64_t min_rcv_time;
	uint64_t max_rcv_time;

};
/*----------------------------------------------------------------------------*/
struct thread_context
{
	int core;
	int ep;

	int target;
	int started;
	int errors;
	int incompletes;
	int done;

	struct sub_stat stat;

	//for connect cmd
	uint64_t sum_con_time;
	uint64_t min_con_time;
	uint64_t max_con_time;

	uint64_t sum_rcv_con_time;
	uint64_t min_rcv_con_time;
	uint64_t max_rcv_con_time;
};
typedef struct thread_context* thread_context_t;
/*----------------------------------------------------------------------------*/
enum mosquitto_state
{
	invalid = -1,
	closed,
	connecting,
	connected,
	conacked,
	disconnected,
	will_disconnect
};
/*----------------------------------------------------------------------------*/
struct mosquitto_var {
	struct mosquitto *mosq;
	thread_context_t ctx;
	int fd;
	enum mosquitto_state mosq_state;

	uint msg_count;
	char *topic;
	char *client_id;
	int id;
	bool is_recv_st;
};
/*----------------------------------------------------------------------------*/
static struct thread_context *g_ctx[MAX_CPUS];
static struct sub_stat *g_stat[MAX_CPUS];
static struct sub_stat acc_stat;
/*----------------------------------------------------------------------------*/
static void CloseConnection(thread_context_t ctx, struct mosquitto_var* mosq_var);
/*----------------------------------------------------------------------------*/
void 
my_connect_callback(struct mosquitto *mosq, void *obj, int result)
{
	struct mosquitto_var *mosq_var = obj;

	if (result == 0) {
		mosq_var->mosq_state = conacked;
		mosq_var->ctx->stat.connects++;
		//set to publish
		struct epoll_event ev;
        	ev.events = EPOLLOUT | EPOLLIN;
        	ev.data.ptr = mosq_var;
        	epoll_ctl(mosq_var->ctx->ep, EPOLL_CTL_MOD, mosq_var->fd, &ev);
	}
}
/*----------------------------------------------------------------------------*/
void 
my_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
	struct mosquitto_var *mosq_var = obj;

	if (msg->payloadlen > sizeof(struct timeval)) {
		struct sub_stat *stat = &(mosq_var->ctx->stat);

		mosq_var->msg_count++;
		stat->rcv_msgs++;

		struct timeval recv_tv, cur_tv;

		gettimeofday(&cur_tv, NULL);
		memcpy(&recv_tv, msg->payload, sizeof(struct timeval));
		uint64_t time_diff = (cur_tv.tv_sec - recv_tv.tv_sec) * 1000000 + cur_tv.tv_usec - recv_tv.tv_usec;

		if (stat->min_rcv_time == 0 || time_diff < stat->min_rcv_time)
			stat->min_rcv_time = time_diff;
		if (time_diff > stat->max_rcv_time)
			stat->max_rcv_time = time_diff;
		stat->sum_rcv_time += time_diff;

		if (mosq_var->is_recv_st) {
			mosq_var->is_recv_st = FALSE;

			char *payload = (char*)msg->payload;
			memcpy(&recv_tv, &(payload[sizeof(struct timeval)]), sizeof(struct timeval));
			time_diff = (cur_tv.tv_sec - recv_tv.tv_sec) * 1000000 + cur_tv.tv_usec - recv_tv.tv_usec;

			thread_context_t ctx = mosq_var->ctx;
			if (ctx->min_rcv_con_time == 0 || time_diff < ctx->min_rcv_con_time)
                        	ctx->min_rcv_con_time = time_diff;
                	if (time_diff > ctx->max_rcv_con_time)
                        	ctx->max_rcv_con_time = time_diff;
                	ctx->sum_rcv_con_time += time_diff;
		}
	}
	if (mosq_var->msg_count == flows_per_con) {
		mosq_var->mosq_state = will_disconnect;
		mosq_var->ctx->stat.completes++;
	}
}
/*----------------------------------------------------------------------------*/
thread_context_t 
CreateContext(int core)
{
	thread_context_t ctx;

	ctx = (thread_context_t)calloc(1, sizeof(struct thread_context));
	if (!ctx) {
		perror("malloc");
		TRACE_ERROR("Failed to allocate memory for thread context.\n");
		return NULL;
	}
	ctx->core = core;

	return ctx;
}
/*----------------------------------------------------------------------------*/
void 
DestroyContext(thread_context_t ctx) 
{
	free(ctx);
}
/*----------------------------------------------------------------------------*/
static inline int 
CreateConnection(thread_context_t ctx)
{
	struct epoll_event ev;
	int sockid;
	int ret;
	struct timeval str_con_tv, end_con_tv;

	struct mosquitto_var *mosq_var = malloc(sizeof(struct mosquitto_var));
	memset(mosq_var, 0, sizeof(struct mosquitto_var));
	mosq_var->ctx = ctx;

	mosq_var->id = ctx->core * concurrency + ctx->started;
	if (ctx->started == concurrency && concurs[ctx->core] > concurs[core_limit - 1])
		mosq_var->id = core_limit * concurs[core_limit - 1] + ctx->core;

	char top[128];
	if (is_unique)
		sprintf(top, "%s%d", topic, mosq_var->id);
	else
		sprintf(top, "%s", topic);
	mosq_var->topic = strdup(top);

	char c_id[128];
	if (client_id != NULL)
		sprintf(c_id, "%s-%d", client_id, mosq_var->id);
	else
		sprintf(c_id, "subscriber-%d", mosq_var->id);

	struct mosquitto *mosq = mosquitto_new(c_id, TRUE, mosq_var);
	mosq_var->mosq = mosq;

	gettimeofday(&str_con_tv, NULL);
	ret = mosquitto_connect(mosq, host, ntohs(dport), 3000);//50min
        if (ret != MOSQ_ERR_SUCCESS) {
		TRACE_CONFIG("mosquitto connect error\n");
		return -1;
	}
	gettimeofday(&end_con_tv, NULL);

	uint64_t con_time = (end_con_tv.tv_sec - str_con_tv.tv_sec) * 1000000 + end_con_tv.tv_usec - str_con_tv.tv_usec;
	if (ctx->min_con_time == 0 || ctx->min_con_time > con_time)
		ctx->min_con_time = con_time;
	if (ctx->max_con_time < con_time)
		ctx->max_con_time = con_time;
	ctx->sum_con_time += con_time;
	mosq_var->is_recv_st = TRUE;
	mosq_var->mosq_state = connected;

	mosquitto_connect_callback_set(mosq, my_connect_callback);
	mosquitto_message_callback_set(mosq, my_message_callback);

	ctx->started++;

	sockid = mosquitto_socket(mosq);
	//TRACE_CONFIG("create socket(%d)\n", sockid);
        if (sockid < 0) {
                TRACE_CONFIG("Failed to create socket(%d)\n", sockid);
                return -1;
        }
	mosq_var->fd = sockid;

	//mosquitto_connect already set non_block
        /*int flag = fcntl(sockid, F_GETFL, 0);
        ret = fcntl(sockid, F_SETFL, flag | O_NONBLOCK);
        if (ret < 0) {
                TRACE_CONFIG("Failed to set socket in nonblocking mode.\n");
                exit(-1);
        }*/

	ev.events = EPOLLIN;
        ev.data.ptr = mosq_var;
	epoll_ctl(ctx->ep, EPOLL_CTL_ADD, sockid, &ev);

	return sockid;
}
/*----------------------------------------------------------------------------*/
static inline void 
CloseConnection(thread_context_t ctx, struct mosquitto_var* mosq_var)
{
	if (mosq_var == NULL)
		return;

	epoll_ctl(ctx->ep, EPOLL_CTL_DEL, mosq_var->fd, NULL);
	//re-initiate insead of re-create
	mosquitto_destroy(mosq_var->mosq);
	mosq_var->mosq_state = closed;
	free(mosq_var);
	mosq_var = NULL;

	ctx->done++;
}
/*----------------------------------------------------------------------------*/
static void 
PrintStats()
{
	struct sub_stat total = {0};
        struct sub_stat *st;
	uint64_t total_avg_time = 0;
	uint64_t avg_time = 0;
        int i;

        for (i = 0; i < core_limit; i++) {
                st = g_stat[i];

                total.waits += st->waits;
                total.events += st->events;
                total.rcv_msgs += st->rcv_msgs;
                total.errors += st->errors;
		if (total.min_rcv_time == 0 || st->min_rcv_time < total.min_rcv_time)
			total.min_rcv_time = st->min_rcv_time;
		if (st->max_rcv_time > total.max_rcv_time)
			total.max_rcv_time = st->max_rcv_time;
		avg_time = (st->rcv_msgs)? st->sum_rcv_time / st->rcv_msgs : 0;
		total_avg_time += avg_time;

                acc_stat.connects += st->connects;
                acc_stat.rcv_msgs += st->rcv_msgs;
                acc_stat.completes += st->completes;
                acc_stat.errors += st->errors;

                memset(st, 0, sizeof(struct sub_stat));
        }
        fprintf(stderr, "[ ALL ] connect: %5lu/ %5lu,    "
                        "rcv_pkts: %7lu pkt/s (%8lu/%8lu),    "
                        "rcv_time (avg:%6lu, min:%6lu, max:%6lu) usec,    "
                        "completes(error): %5lu( %5lu)/%5lu\n",
                        acc_stat.connects, total_concurrency,
                        total.rcv_msgs, acc_stat.rcv_msgs, total_flows,
			total_avg_time / core_limit, total.min_rcv_time, total.max_rcv_time,
                        acc_stat.completes, acc_stat.errors, total_concurrency);

#if 0
	fprintf(stderr, "[ ALL ] epoll_wait: %5lu, event: %7lu, "
			"connect: %7lu, read: %4lu MB, write: %4lu MB, "
			"completes: %7lu (resp_time avg: %4lu, max: %6lu us), "
			"errors: %2lu (timedout: %2lu)\n", 
			total.waits, total.events, total.connects, 
			total.reads / 1024 / 1024, total.writes / 1024 / 1024, 
			total.completes, total_resp_time / core_limit, total.max_resp_time, 
			total.errors, total.timedout);
#endif
}
/*----------------------------------------------------------------------------*/
void *
RunMain(void *arg)
{
	thread_context_t ctx;
	int core = *(int *)arg;
	struct in_addr daddr_in;
	int n, maxevents;
	int ep;
	struct epoll_event *events;
	int nevents;
	int i;
	bool is_first;

	struct timeval cur_tv, prev_tv;

	AffinitizeThreadToCore(core);

	ctx = CreateContext(core);
	if (!ctx) {
		return NULL;
	}
	g_ctx[core] = ctx;
	g_stat[core] = &ctx->stat;
	srand(time(NULL));

	n = concurs[core];
	if (n == 0) {
		TRACE_DBG("Application thread %d finished.\n", core);
		pthread_exit(NULL);
		return NULL;
	}
	ctx->target = n;

	daddr_in.s_addr = daddr;
	fprintf(stderr, "Thread %d handles %d concurrencies. connecting to %s:%u\n", 
			core, n, inet_ntoa(daddr_in), ntohs(dport));

	/* Initialization */
	maxevents = max_fds * 3;
	ep = epoll_create(maxevents);

	if (ep < 0) {
		TRACE_ERROR("Failed to create epoll struct!n");
		exit(EXIT_FAILURE);
	}
	events = (struct epoll_event *)calloc(maxevents, sizeof(struct epoll_event));
	if (!events) {
		TRACE_ERROR("Failed to allocate events!\n");
		exit(EXIT_FAILURE);
	}
	ctx->ep = ep;

	ctx->started = ctx->done = 0;
	ctx->errors = ctx->incompletes = 0;

	gettimeofday(&cur_tv, NULL);
	prev_tv = cur_tv;
	is_first = TRUE;

	while (!done[core]) {
		gettimeofday(&cur_tv, NULL);

		/* print statistics every second */
		if (core == 0 && cur_tv.tv_sec > prev_tv.tv_sec) {
			if (is_first && g_stat[0]->rcv_msgs > 0) {
				is_first = FALSE;
				gettimeofday(&exec_stv, NULL);
			}
			PrintStats();
			prev_tv = cur_tv;
		}

		while (ctx->started < ctx->target) {
			if (CreateConnection(ctx) < 0) {
				done[core] = TRUE;
				break;
			}
		}

		nevents = epoll_wait(ep, events, maxevents, 1);

		if (nevents < 0) {
			if (errno != EINTR) {
				TRACE_CONFIG("epoll_wait failed! ret: %d\n", nevents);
			}
			done[core] = TRUE;
			break;
		} else {
			ctx->stat.events += nevents;
		}

		for (i = 0; i < nevents; i++) {

			if (events[i].events & EPOLLERR) {
				struct mosquitto_var *mosq_var = events[i].data.ptr;
				TRACE_CONFIG("[CPU %d] Error on socket %d\n", core, mosq_var->fd);
				ctx->stat.errors++;
				ctx->errors++;
				CloseConnection(ctx, mosq_var);
				close(mosq_var->fd);

			} else if (events[i].events & EPOLLIN) {
				struct mosquitto_var *mosq_var = events[i].data.ptr;
//fprintf(stdout, "[IN %d]\n", mosquitto_socket(mosq_var->mosq));
				int ret = mosquitto_loop_read(mosq_var->mosq, 1);
				if (ret != MOSQ_ERR_SUCCESS) {
                                        fprintf(stdout, "loop read error %d\n", mosq_var->fd);
					ctx->stat.errors++;
					CloseConnection(ctx, mosq_var);
				}
				if (mosq_var->mosq_state == will_disconnect) {
					mosquitto_disconnect(mosq_var->mosq);
					CloseConnection(ctx, mosq_var);
				}

			} else if (events[i].events == EPOLLOUT) {
				struct mosquitto_var *mosq_var = events[i].data.ptr;
//fprintf(stdout, "[OUT %d]\n", mosq_var->fd);
				if (mosq_var->mosq_state == connecting)
					continue;

				int ret = mosquitto_subscribe(mosq_var->mosq, &(mosq_var->id), mosq_var->topic, 0);
				if (ret == MOSQ_ERR_SUCCESS) {
                                	ret = mosquitto_loop_write(mosq_var->mosq, 1);
                                }
                                if (ret != MOSQ_ERR_SUCCESS) {
                                        fprintf(stdout, "loop write error %d\n", mosq_var->fd);
                                        ctx->stat.errors++;
					CloseConnection(ctx, mosq_var);
					continue;
                                }
				struct epoll_event ev;
        			ev.events = EPOLLIN;
        			ev.data.ptr = mosq_var;
        			epoll_ctl(mosq_var->ctx->ep, EPOLL_CTL_MOD, mosq_var->fd, &ev);

			} else {
				TRACE_ERROR("Socket %d: event: %s\n", mosq_var->fd, EventToString(events[i].events));
				assert(0);
			}
		}
		if (ctx->done >= ctx->target) {
			//fprintf(stdout, "[CPU %d] Completed %d connections, "
			//		"errors: %d incompletes: %d\n", 
			//		ctx->core, ctx->done, ctx->errors, ctx->incompletes);
			break;
		}
	}

	//TRACE_CONFIG("sub thread %d waiting for mosquitto to be destroyed.\n", core);
	//DestroyContext(ctx);

	//TRACE_DBG("sub thread %d finished.\n", core);
	pthread_exit(NULL);
	return NULL;
}
/*----------------------------------------------------------------------------*/
void 
SignalHandler(int signum)
{
	int i;
	for (i = 0; i < core_limit; i++) {
		done[i] = TRUE;
	}
}
/*----------------------------------------------------------------------------*/
int 
main(int argc, char **argv)
{
	int cores[MAX_CPUS];
	int concurrency_remainder_cnt;
	int i;

	if (argc < 3) {
		TRACE_CONFIG("Too few arguments!\n");
		TRACE_CONFIG("Usage: ./epsub 10.1.128.1 200000 -p 1000 -c sub3 -u -t 1/2/3\n");
		return FALSE;
	}

	strncpy(host, argv[1], MAX_IP_STR_LEN);

	daddr = inet_addr(host);
	dport = htons(1883);

	flows_per_con = atoi(argv[2]);
	if (flows_per_con <= 0) {
		TRACE_CONFIG("Number of flows should be large than 0.\n");
		return FALSE;
	}

	num_cores = GetNumCPUCores();

	//default parameters
	core_limit = num_cores;
	concurrency = 1;
	is_unique = FALSE;
	client_id = NULL;
	topic = NULL;
	message = NULL;

	for (i = 3; i < argc - 1; i++) {
		if (strcmp(argv[i], "-N") == 0) {
			core_limit = atoi(argv[i + 1]);
			if (core_limit > num_cores) {
				TRACE_CONFIG("CPU limit should be smaller than the "
						"number of CPUS: %d\n", num_cores);
				return FALSE;
			}
		} else if (strcmp(argv[i], "-p") == 0) {
			total_concurrency = atoi(argv[i + 1]);

		} else if (strcmp(argv[i], "-t") == 0) {
			topic = strdup(argv[i + 1]);

		} else if (strcmp(argv[i], "-u") == 0) {
			is_unique = TRUE;

		} else if (strcmp(argv[i], "-c") == 0) {
			client_id = strdup(argv[i + 1]);

		} else if (strcmp(argv[i], "-o") == 0) {
			if (strlen(argv[i + 1]) > MAX_FILE_LEN) {
				TRACE_CONFIG("Output file length should be smaller than %d!\n", 
						MAX_FILE_LEN);
				return FALSE;
			}
			fio = TRUE;
			strncpy(outfile, argv[i + 1], MAX_FILE_LEN);
		}
	}

	//validate arguments
	if (topic == NULL) {
		fprintf(stdout, "topic is NULL\n");
		return FALSE;
	}

	if (total_concurrency < core_limit) {
		core_limit = total_concurrency;
	}

	concurrency_remainder_cnt = 0;
	/* per-core concurrency = total_concurrency / # cores */
	if (total_concurrency > 0) {
		concurrency = total_concurrency / core_limit;
		concurrency_remainder_cnt = total_concurrency % core_limit;
	}

	/* set the max number of fds 2x larger than concurrency */
	max_fds = concurrency * 2;

	total_flows = flows_per_con * total_concurrency;

	TRACE_CONFIG("==========================\n");
	TRACE_CONFIG("Application configuration:\n");
	TRACE_CONFIG("# of waiting msgs: %lu\n", total_flows);
	TRACE_CONFIG("# of cores: %d\n", core_limit);
	TRACE_CONFIG("# of concurrency: %lu\n", total_concurrency);
	if (fio) {
		TRACE_CONFIG("Output file: %s\n", outfile);
	}
	TRACE_CONFIG("==========================\n");

	signal(SIGINT, SignalHandler);

	mosquitto_lib_init();

	for (i = 0; i < core_limit; i++) {
		cores[i] = i;
		done[i] = FALSE;
		concurs[i] = concurrency;

		if (concurrency_remainder_cnt-- > 0)
			concurs[i]++;

		if (concurs[i] == 0)
			continue;

		if (pthread_create(&app_thread[i], 
					NULL, RunMain, (void *)&cores[i])) {
			perror("pthread_create");
			TRACE_ERROR("Failed to create pub thread.\n");
			exit(-1);
		}
	}
	for (i = 0; i < core_limit; i++) {
		pthread_join(app_thread[i], NULL);
		TRACE_INFO("pub thread %d joined.\n", i);
	}

	PrintStats();

	TRACE_CONFIG("====================================================\n");
        gettimeofday(&exec_etv, NULL);
        fprintf(stdout, "Execution time: %lf seconds\n", (exec_etv.tv_sec - exec_stv.tv_sec) + (double)(exec_etv.tv_usec - exec_stv.tv_usec) / 1000000);

	//destroy ctx after printing last stats because of ctx->stats
	uint64_t sum_time = 0;
	uint64_t min_time = 0;
	uint64_t max_time = 0;
        for (i = 0; i < core_limit; i++) {
		sum_time += g_ctx[i]->sum_con_time;
		if (min_time == 0 || min_time > g_ctx[i]->min_con_time)
			min_time = g_ctx[i]->min_con_time;
		if (max_time < g_ctx[i]->max_con_time)
			max_time = g_ctx[i]->max_con_time;
        }
	fprintf(stdout, "Connection time: avg:%lu, min:%lu, max:%lu usec\n", sum_time/total_concurrency, min_time, max_time);

	sum_time = 0;
	min_time = 0;
	max_time = 0;
	for (i = 0; i < core_limit; i++) {
                sum_time += g_ctx[i]->sum_rcv_con_time;
                if (min_time == 0 || min_time > g_ctx[i]->min_rcv_con_time)
                        min_time = g_ctx[i]->min_rcv_con_time;
                if (max_time < g_ctx[i]->max_rcv_con_time)
                        max_time = g_ctx[i]->max_rcv_con_time;
                DestroyContext(g_ctx[i]);
        }
	fprintf(stdout, "Latency+connection avg:%lu, min:%lu, max:%lu usec\n", sum_time/total_concurrency, min_time, max_time);

	mosquitto_lib_cleanup();

	//free parameters
	if (topic != NULL)
		free(topic);
	if (client_id != NULL)
		free(client_id);
	if (size != 0 && message != NULL)
		free(message);

	return 0;
}
/*----------------------------------------------------------------------------*/
