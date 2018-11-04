/*
 * Copyright (c) 2014 Putilov Andrey
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of ths software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and ths permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
//#include <signal.h>
#include <sys/ioctl.h>
#include "websocket.h"
#include <errno.h>
#include <pthread.h>
#include "list.h"
#include "monitor.h"

#define PORT 8088
#define BUF_LEN 1024

//#define PACKET_DUMP
#ifndef bool
	#define bool int
#endif
#ifndef true
	#define true 1
#endif
#ifndef false
	#define false 0
#endif

#define PRINTF(fmt, ...) printf("#%s: " fmt, __func__, ##__VA_ARGS__)

typedef struct {
	int clientSocket;
	int *count;
	uint8_t *gBuffer;
	struct sockaddr_in clientAddr;
	bool isContinue;
	pthread_t ws_thread;
	pthread_mutex_t ws_mutex;
	list_head list;
} WEBSOCKET_PARAM;

LIST_HEAD(sock_list);
WEBSOCKET_PARAM *sock=NULL;
int sock_count=0;
int listenSocket;

bool isContinue = false;

static pthread_t MonitorHandle = (pthread_t) NULL;
static pthread_mutex_t MonitorLock;
static char *OutputMessage;

char *showFrame(int type)
{
    char *p = NULL;
    switch(type) {
    case WS_EMPTY_FRAME:
            p = "WS_EMPTY_FRAME";
            break;
    case WS_ERROR_FRAME:
            p = "WS_ERROR_FRAME";
            break;
    case WS_INCOMPLETE_FRAME:
            p = "WS_INCOMPLETE_FRAME";
            break;
    case WS_TEXT_FRAME:
            p = "WS_TEXT_FRAME";
            break;
    case WS_BINARY_FRAME:
            p = "WS_BINARY_FRAME";
            break;
    case WS_PING_FRAME:
            p = "S_PING_FRAME";
            break;
    case WS_PONG_FRAME:
            p = "WS_PONG_FRAME";
            break;
    case WS_OPENING_FRAME:
            p = "WS_OPENING_FRAME";
            break;
    case WS_CLOSING_FRAME:
            p = "WS_CLOSING_FRAME";
            break;
    }
    return p;
}

char *showState(int state)
{
    char *p = NULL;
    switch(state) {
    case WS_STATE_OPENING:
        p = "WS_STATE_OPENING";
        break;
    case WS_STATE_NORMAL:
        p = "WS_STATE_NORMAL";
        break;
    case WS_STATE_CLOSING:
        p = "WS_STATE_CLOSING";
        break;
    }
    return p;
};

void error(const char *msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

int MonitorMessage(char *message)
{
	if (sock_count > 0) {
		PRINTF("SetMessage=%s\n", message);
		OutputMessage = message;
		pthread_mutex_unlock(&MonitorLock);
		return 1;
	}
	else {
		PRINTF("MonitorMessage: No connection\n");
		return 0;
	}
}

int OutputBuffer(uint8_t **p)
{
	char *buffer;
        pthread_mutex_lock(&MonitorLock);

	if (OutputMessage == NULL) {
		PRINTF("OutputMessage == NULL\n");
		return 0;
	}
	else if (strlen(OutputMessage) == 0) {
		PRINTF("OutputMessage length == 0\n");
		return 0;
	}		
	buffer = strdup(OutputMessage);
	if (buffer == NULL) {
		fprintf(stderr, "strdup error\n");
		return 0;
	}
	*p = buffer;
	PRINTF("len=%d\n", (int) strlen(buffer));
	return (strlen(buffer));
}

int safeSend(WEBSOCKET_PARAM *param, const uint8_t *buffer, size_t bufferSize)
{
#ifdef PACKET_DUMP
	PRINTF("out packet [\n");
	fwrite(buffer, 1, bufferSize, stdout);
	PRINTF("]\n\n");
#endif
	pthread_mutex_lock(&param->ws_mutex);
	ssize_t written = send(param->clientSocket, buffer, bufferSize, 0);
	pthread_mutex_unlock(&param->ws_mutex);
	//free(buffer);
	if (written == -1) {
		return EXIT_FAILURE;
	}
	if (written != bufferSize) {
		return EXIT_FAILURE;
	}
	PRINTF("End OK\n");
	return EXIT_SUCCESS;
}

void safeSendAll(const uint8_t *buffer, size_t bufferSize) {
	int result;
	WEBSOCKET_PARAM *data,*dend;
	PRINTF("Enter\n");
    list_for_each_entry_safe(data,dend,&sock_list,list){
		if(safeSend(data,buffer,bufferSize)!=EXIT_SUCCESS) {
			data->isContinue = false;
		}
    }
}

void *clientWorker(void *param)
{
	WEBSOCKET_PARAM *ths = (WEBSOCKET_PARAM *)param;
	ths->isContinue = true;
	memset(ths->gBuffer, 0, BUF_LEN);
	size_t readedLength = 0;
	size_t frameSize = BUF_LEN;
	enum wsState state = WS_STATE_OPENING;
	uint8_t *data = NULL;
	size_t dataSize = 0;
	enum wsFrameType frameType = WS_INCOMPLETE_FRAME;
	struct handshake hs;
	nullHandshake(&hs);

#define prepareBuffer frameSize = BUF_LEN; memset(ths->gBuffer, 0, BUF_LEN);
#define initNewFrame frameType = WS_INCOMPLETE_FRAME; readedLength = 0; memset(ths->gBuffer, 0, BUF_LEN);

    pthread_mutex_lock(&MonitorLock);
	while (isContinue && ths->isContinue &&(frameType == WS_INCOMPLETE_FRAME)) {
        PRINTF("While frame=%s state=%s\n", showFrame(frameType), showState(state));

        if (state == WS_STATE_OPENING) {
			ssize_t readed = recv(ths->clientSocket, ths->gBuffer+readedLength, BUF_LEN-readedLength, MSG_DONTWAIT);
			if((readed == -EAGAIN) || (readed == -EWOULDBLOCK)){
				PRINTF("**readed = %ld\n",readed);
				perror("recv failed");
				break;
			} else if(readed <= 0) {
				PRINTF("100ms Wait...\n");
				usleep(1000*100);
				continue;
			}
	#ifdef PACKET_DUMP
			PRINTF("in packet: [\n");
			fwrite(ths->gBuffer, 1, readed, stdout);
			PRINTF("]\n\n");
	#endif
			readedLength+= readed;
			assert(readedLength <= BUF_LEN);
		}
		if (state == WS_STATE_OPENING) {
			frameType = wsParseHandshake(ths->gBuffer, readedLength, &hs);
            PRINTF("wsParseHandshake frame=%s\n", showFrame(frameType));			
		} else {
			//frameType = wsParseInputFrame(ths->gBuffer, readedLength, &data, &dataSize);
			//PRINTF("wsParseInputFrame frame=%s\n", showFrame(frameType));
		    frameType = WS_TEXT_FRAME;
	    	PRINTF("New Frame frame=%s\n", showFrame(frameType));			
		}

        PRINTF("Middle frame=%s state=%s\n",
	       showFrame(frameType), showState(state));

		if ((frameType == WS_INCOMPLETE_FRAME && readedLength == BUF_LEN)
			|| frameType == WS_ERROR_FRAME) {
			if (frameType == WS_INCOMPLETE_FRAME)
				printf("buffer too small");
			else
				printf("error in incoming frame\n");

			if (state == WS_STATE_OPENING) {
				prepareBuffer;
				frameSize = sprintf((char *)ths->gBuffer,
						"HTTP/1.1 400 Bad Request\r\n"
						"%s%s\r\n\r\n",
						versionField,
						version);
				safeSendAll(ths->gBuffer, frameSize);
                PRINTF("safeSendAll Open break\n");				
				break;
			} else {
				prepareBuffer;
				wsMakeFrame(NULL, 0, ths->gBuffer, &frameSize, WS_CLOSING_FRAME);
				if (safeSend(ths,ths->gBuffer, frameSize) == EXIT_FAILURE) {
				    PRINTF("safeSend Faile break(1)\n");
                    break;
				}
				state = WS_STATE_CLOSING;
				initNewFrame;
                PRINTF("initNewFrame(0)\n");				
			}
		}

        PRINTF("2: state=%s\n", showState(state));
		if (state == WS_STATE_OPENING) {
			assert(frameType == WS_OPENING_FRAME);
			if (frameType == WS_OPENING_FRAME) {
				// if resource is right, generate answer handshake and send it
				if (strcmp(hs.resource, "/echo") != 0) {
					frameSize = sprintf((char *)ths->gBuffer, "HTTP/1.1 404 Not Found\r\n\r\n");
					safeSend(ths,ths->gBuffer, frameSize);
					PRINTF("safeSend Notfound break\n");
					break;
				}

				prepareBuffer;
				wsGetHandshakeAnswer(&hs, ths->gBuffer, &frameSize);
				freeHandshake(&hs);
				if (safeSend(ths,ths->gBuffer, frameSize) == EXIT_FAILURE) {
		    		PRINTF("safeSend Faile break(2)\n");
		    		break;
				}
				PRINTF("safeSend normal end\n");
				state = WS_STATE_NORMAL;
				initNewFrame;
                PRINTF("initNewFrame(1)\n"); // for Opening				
			}
		} else {
            PRINTF("3: frame=%s state=%s\n", showFrame(frameType), showState(state));

			if (frameType == WS_CLOSING_FRAME) {
				if (state == WS_STATE_CLOSING) {
		        	PRINTF("state == WS_STATE_CLOSING break\n");					
					break;
				} else {
					prepareBuffer;
					wsMakeFrame(NULL, 0, ths->gBuffer, &frameSize, WS_CLOSING_FRAME);
					if (safeSend(ths,ths->gBuffer, frameSize) ==  EXIT_FAILURE) {
						PRINTF("safeSend Faile break(3)\n");
						break;
					}
		        	PRINTF("safeSend close break\n");
                    break;					
				}
			} else if (frameType == WS_TEXT_FRAME) {
				uint8_t *recievedString = NULL;
				dataSize = OutputBuffer(&recievedString);
				if (dataSize == 0) {
					return NULL;
				}
				PRINTF("start wsMakeFrame\n");
				wsMakeFrame(recievedString, dataSize, ths->gBuffer, &frameSize, WS_TEXT_FRAME);
				PRINTF("end wsMakeFrame\n");
				free(recievedString);
				safeSendAll(ths->gBuffer, frameSize);
				initNewFrame;
				PRINTF("initNewFrame(2)\n");				
			}
		}
	} // read/write cycle
	sock_count--;
	PRINTF("*disconnected(%d) %s:%d\n", sock_count,
	       inet_ntoa(ths->clientAddr.sin_addr), ntohs(ths->clientAddr.sin_port));

	close(ths->clientSocket);
	free(ths->gBuffer);
	list_del(&ths->list);
	free(ths);
	pthread_mutex_unlock(&MonitorLock);
	pthread_exit(NULL);
	return NULL;
}

void *MonitorMain(void *Message)
{
	struct sockaddr_in local;
	int on = 1;

	PRINTF("MonitorMain...\n");
	listenSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (listenSocket == -1) {
		error("*create socket failed");
	}
	setsockopt(listenSocket, SOL_SOCKET,  SO_REUSEADDR,&on, sizeof(on));

	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = htons(PORT);
	isContinue = true;

	if (bind(listenSocket, (struct sockaddr *) &local, sizeof(local)) <0 ) {
		error("*bind failed");
	}
	if (listen(listenSocket, 1) == -1) {
		error("*listen failed");
	}

	PRINTF("*Monitor Start...\n");
	while (isContinue) {
		struct sockaddr_in remote;
		int clientSocket;
		socklen_t sockaddrLen = sizeof(remote);

		PRINTF("*waiting connection=%d\n", isContinue);
		clientSocket = accept(listenSocket, (struct sockaddr*)&remote, &sockaddrLen);
		if((clientSocket == -1) || ( clientSocket == -EAGAIN ) || (clientSocket == -EWOULDBLOCK)) {
			usleep(1000);
			continue;
		}
		sock_count++;
		PRINTF("*connect(%d) from %s:%d\n", sock_count,
		       inet_ntoa(remote.sin_addr), ntohs(remote.sin_port));
		// list
		sock = (WEBSOCKET_PARAM *)malloc(sizeof(WEBSOCKET_PARAM));
		sock->count = &sock_count;
		sock->clientSocket = clientSocket;
		sock->gBuffer = (uint8_t *)malloc(BUF_LEN);
		memcpy(&sock->clientAddr,&remote,sizeof(remote));
		pthread_mutex_init(&sock->ws_mutex, NULL);
		INIT_LIST_HEAD(&sock->list);
		list_add_tail(&sock->list, &sock_list);

		(void) pthread_create(&sock->ws_thread, NULL, clientWorker, sock);
		PRINTF("*thread created=%d\n", sock_count);
	}
	WEBSOCKET_PARAM *data,*dend;
	list_for_each_entry_safe(data,dend,&sock_list,list){
		PRINTF("*pthread_join\n");
		(void) pthread_join(data->ws_thread, (void **) NULL);
    }
    // stop thread
    PRINTF("*Monitor Exit...\n");
    MonitorHandle = (pthread_t) NULL;
    return((void *) NULL);
}

int MonitorStart(void)
{
	if (MonitorHandle != (pthread_t) NULL) {
		fprintf(stderr, "Monitor already started\n");
		return 0;
	}
	OutputMessage = NULL;
        pthread_mutex_init(&MonitorLock, NULL);
        pthread_create(&MonitorHandle, NULL, MonitorMain, OutputMessage);
	return 1;
}

void MonitorStop(void) {
	PRINTF("**stop connection\n");
	isContinue = false;
	close(listenSocket);
}
