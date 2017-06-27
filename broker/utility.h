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

#ifndef UTILITY_H
#define UTILITY_H

#include <stdint.h>

#define INVALID_SOCKET -1

typedef enum { false, true } bool;

enum log_level {
    ll_error=0,
    ll_info=1,
    ll_verbose=2,
};

void write_log(enum log_level ll, const char *desc, ...);

#define write_errlog(desc, args...) write_log(ll_error, desc, ## args)
#define write_infolog(desc, args...) write_log(ll_info, desc, ## args)
#define write_debuglog(desc, args...) write_log(ll_verbose, desc, ## args)

void print_hex(bool is_received, unsigned char *msg, int msg_len);

bool pub_topic_valid(const char *str);
bool sub_topic_valid(const char *str);

#define MIN(a, b) ((a)<(b)?(a):(b))

#endif
