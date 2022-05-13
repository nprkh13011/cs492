#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include "scull.h"
#include <pthread.h>

#define CDEV_NAME "/dev/scull"

/* Quantum command line option */
static int g_quantum;

static void usage(const char *cmd)
{
	printf("Usage: %s <command>\n"
	       "Commands:\n"
	       "  R          Reset quantum\n"
	       "  S <int>    Set quantum\n"
	       "  T <int>    Tell quantum\n"
	       "  G          Get quantum\n"
	       "  Q          Query quantum\n"
	       "  X <int>    Exchange quantum\n"
	       "  H <int>    Shift quantum\n"
	       "  h          Print this message\n"
	       "  i          Get Info of Process\n"
	       "  p 	     Print process\n",
	       cmd);
}

typedef int cmd_t;

static cmd_t parse_arguments(int argc, const char **argv)
{
	cmd_t cmd;

	if (argc < 2) {
		fprintf(stderr, "%s: Invalid number of arguments\n", argv[0]);
		cmd = -1;
		goto ret;
	}

	/* Parse command and optional int argument */
	cmd = argv[1][0];
	switch (cmd) {
	case 'S':
	case 'T':
	case 'H':
	case 'X':
		if (argc < 3) {
			fprintf(stderr, "%s: Missing quantum\n", argv[0]);
			cmd = -1;
			break;
		}
		g_quantum = atoi(argv[2]);
		break;
	case 'R':
	case 'G':
	case 'Q':
	case 'i':
	case 'p':
	case 't':
	case 'h':
		break;
	default:
		fprintf(stderr, "%s: Invalid command\n", argv[0]);
		cmd = -1;
	}

ret:
	if (cmd < 0 || cmd == 'h') {
		usage(argv[0]);
		exit((cmd == 'h')? EXIT_SUCCESS : EXIT_FAILURE);
	}
	return cmd;
}
/* helper - print threads */
void* print_threads (void* arg) {
	struct task_info t;
	int ret, i;
	/*call ioctl 2 times*/
	for (i=0; i<2; i++){
		ret = ioctl(arg, SCULL_IOCIQUANTUM, &t);
		printf("state %ld, stack %p, cpu %u, prio %d, sprio %d, nprio %d, rtprio %u, pid %d, tgid %d, nv %lu, niv %lu\n", t.state, t.stack, t.cpu, t.prio, t.static_prio, t.normal_prio, t.rt_priority, t.pid, t.tgid, t.nvcsw, t.nivcsw);
	}
	pthread_exit(0); /* exit to ensure all threads are done */

}

static int do_op(int fd, cmd_t cmd)
{
	pid_t pid;
	pthread_t arr[4];
	int ret, q, i;
	int status;
	struct task_info t;
	switch (cmd) {
	case 'R':
		ret = ioctl(fd, SCULL_IOCRESET);
		if (ret == 0)
			printf("Quantum reset\n");
		break;
	case 'Q':
		q = ioctl(fd, SCULL_IOCQQUANTUM);
		printf("Quantum: %d\n", q);
		ret = 0;
		break;
	case 'G':
		ret = ioctl(fd, SCULL_IOCGQUANTUM, &q);
		if (ret == 0)
			printf("Quantum: %d\n", q);
		break;
	case 'T':
		ret = ioctl(fd, SCULL_IOCTQUANTUM, g_quantum);
		if (ret == 0)
			printf("Quantum set\n");
		break;
	case 'S':
		q = g_quantum;
		ret = ioctl(fd, SCULL_IOCSQUANTUM, &q);
		if (ret == 0)
			printf("Quantum set\n");
		break;
	case 'X':
		q = g_quantum;
		ret = ioctl(fd, SCULL_IOCXQUANTUM, &q);
		if (ret == 0)
			printf("Quantum exchanged, old quantum: %d\n", q);
		break;
	case 'H':
		q = ioctl(fd, SCULL_IOCHQUANTUM, g_quantum);
		printf("Quantum shifted, old quantum: %d\n", q);
		ret = 0;
		break;
	case 'i':
		ret = ioctl(fd, SCULL_IOCIQUANTUM, &t);
		if (ret == 0) /*similar to G, print out the information */
			printf("state %ld, stack %p, cpu %u, prio %d, sprio %d, nprio %d, rtprio %u, pid %d,tgid %d,  nv %lu, niv %lu\n", t.state, t.stack, 
			      t.cpu, t.prio, t.static_prio, t.normal_prio, 
			     t.rt_priority, t.pid, t.tgid, t.nvcsw, t.nivcsw);	
		break;
	case 'p':
		ret = -1;
		pid = 1; /*must be initialized but can't be 0 so 1 lol */
		
		for (i=0; i<4; i++){
			/*fork 4 times, make sure child process is not forked*/
			pid=fork();
			if (pid == 0 ) { /* print 2x the child processes  */
				ret = ioctl(fd, SCULL_IOCIQUANTUM, &t);
				if (ret == 0) {
					printf("state %ld, stack %p, cpu %u, prio %d, sprio %d, nprio %d, rtprio %u, pid %d, tgid %d, nv %lu, niv %lu\n", t.state, t.stack, t.cpu, t.prio, t.static_prio, t.normal_prio, t.rt_priority, t.pid, t.tgid, t.nvcsw, t.nivcsw);
				}
				if (ret == 0) {
					printf("state %ld, stack %p, cpu %u, prio %d, sprio %d, nprio %d, rtprio %u, pid %d, tgid %d, nv %lu, niv %lu\n", t.state, t.stack, t.cpu, t.prio, t.static_prio, t.normal_prio, t.rt_priority, t.pid, t.tgid, t.nvcsw, t.nivcsw);
				}
				exit(0);
			}
		}
			
	/*	wait for the number of processes */
		while (i != 0){
			wait(&status);
			i--;	
		}
		ret = 0;
		break;
	case 't':
	/*create 4 threads and helper function is applied to the pthread_create
	 * pthread_create (thread, attr, *start_routine, arg)
	 */	
		for (i=0; i<4; i++){
			pthread_create(&arr[i], NULL, print_threads, fd); 
		}
		for (i=0; i<4;i++){
			pthread_join(arr[i],NULL);
		}
		ret =0;
		break;
	default:	
		/* Should never occur */
		abort();
		ret = -1; /* Keep the compiler happy */
	}

	if (ret != 0)
		perror("ioctl");
	return ret;
}

int main(int argc, const char **argv)
{
	int fd, ret;
	cmd_t cmd;

	cmd = parse_arguments(argc, argv);

	fd = open(CDEV_NAME, O_RDONLY);
	if (fd < 0) {
		perror("cdev open");
		return EXIT_FAILURE;
	}

	printf("Device (%s) opened\n", CDEV_NAME);

	ret = do_op(fd, cmd);

	if (close(fd) != 0) {
		perror("cdev close");
		return EXIT_FAILURE;
	}

	printf("Device (%s) closed\n", CDEV_NAME);

	return (ret != 0)? EXIT_FAILURE : EXIT_SUCCESS;
}
