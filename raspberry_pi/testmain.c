#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "monitor.h"

//#define PORT 8088
//#define BUF_LEN 1024

//#define PACKET_DUMP
#define error(msg) { \
        perror(msg); \
        exit(EXIT_FAILURE); \
}

void handler(int signo)
{
	if (signo != 0) {
		printf("\n*received %d\n", signo);
		MonitorStop();
	} else {
		printf("*catch signal hander of %d\n",signo);
	}
	exit(0);
}

int main(int ac,char *av[])
{
	int i;
	int times = 5;
	char buf[BUFSIZ];
	
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		error("*SIGPIPE error\n");
	}

	if (signal(SIGINT, handler) == SIG_ERR) {
		error("*SIGINT error\n");
	}

	if (signal(SIGTERM, handler) == SIG_ERR) {
		error("*SIGTERM error\n");
	}
#if 0
	if (signal(SIGABRT, handler) == SIG_ERR) {
		error("*SIGABRT error\n");
	}
	if (signal(SIGHUP, handler) == SIG_ERR) {
		error("*SIGHUP error\n");
	}
	if (signal(SIGILL, handler) == SIG_ERR) {
		error("*SIGILL error\n");
	}
	if (signal(SIGSEGV, handler) == SIG_ERR) {
		error("*SIGSEGV error\n");
	}
#endif
	if (ac > 1) {
		times = atoi(av[1]);
	}

	printf("test start times=%d\n", times);
	
	MonitorStart();
	while(1) {
		int result;
		for(i = 0; i < times; i++) {
			sleep(i);
			sprintf(buf, "Message-%d", i);
			printf("T:%d\n", i);
			result = MonitorMessage(buf);
			if (result)
				printf("OK!\n");
			else 
				printf("FAILED!\n");
		}
	}

	MonitorStop();
	printf("test stop\n");
	return(0);
}
