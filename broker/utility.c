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

#include "utility.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

void write_log(enum log_level ll, const char *desc, ...) {
    char buffer[1048*1048];
    va_list args;
    
    va_start(args, desc);
    vsprintf(buffer, desc, args);
    va_end(args);
    if (ll == ll_info) {
        fprintf(stdout, "info: %s\n", buffer);
    } else if (ll == ll_verbose) {
        fprintf(stdout, "debug: %s\n", buffer);
    } else if (ll == ll_error) {
        fprintf(stdout, "error: %s\n", buffer);
    }
}

void print_hex(bool is_received, unsigned char *msg, int msg_len) {
    char buff[1048*1048];
    if (is_received) {
        sprintf(buff, "received <<<");
    } else {
        sprintf(buff, "sent >>>");
    }
    for (int i = 0; i < msg_len; i ++) {
        char ch[32];
        sprintf(ch, " [%d]%02x", i, msg[i]);
        strcat(buff, ch);
    }
    write_debuglog(buff);
}

bool pub_topic_valid(const char *str) {
	int len = 0;
	while (str && str[0]) {
		if (str[0] == '+' || str[0] == '#') {
			return false;
		}
		len++;
		str = &str[1];
	}
	if (len > 65535)
        return false;

	return true;
}

bool sub_topic_valid(const char *str) {
	char c = '\0';
	int len = 0;
	while (str && str[0]) {
		if (str[0] == '+') {
			if ((c != '\0' && c != '/') || (str[1] != '\0' && str[1] != '/')) {
				return false;
			}
		} else if(str[0] == '#') {
			if ((c != '\0' && c != '/')  || str[1] != '\0') {
				return false;
			}
		}
		len++;
		c = str[0];
		str = &str[1];
	}
	if (len > 65535)
        return false;

	return true;
}
