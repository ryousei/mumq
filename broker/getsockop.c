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

#include <sys/socket.h>
#include <stdio.h>
#include <netinet/tcp.h>

int main(int argc, char **argv)
{
 int sockfd, sendbuff;
 socklen_t optlen;

 sockfd = socket(AF_INET, SOCK_STREAM, 0);
 if(sockfd == -1)
     printf("Error");

 int res = 0;

 optlen = sizeof(sendbuff);
 res = getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sendbuff, &optlen);
 printf("snd buffer size = %d\n", sendbuff);
 res = getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &sendbuff, &optlen);
 printf("rcv buffer size = %d\n", sendbuff);
 res = getsockopt(sockfd, SOL_SOCKET, TCP_NODELAY, &sendbuff, &optlen);
 printf("TCP NO DELAY = %d\n", sendbuff);
 res = getsockopt(sockfd, SOL_SOCKET, TCP_CORK, &sendbuff, &optlen);
 printf("TCP CROK = %d\n", sendbuff);
 return 0;
}
