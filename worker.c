#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <getopt.h> 
#include <unistd.h>
#include <signal.h>
#include <string.h> 
#include <errno.h>

const size_t BUFF_SZ = sizeof(int) * 2;

struct Message {
    long mtype;
    int status;
};

int main (int argc, char *argv[]) {
    //Implement oss initialization of shared memory and worker being able to take in arguments. At this stage, just make sure
    //that worker can read the shared memory clock. [Day 3]

    if (argc != 3) {
        fprintf(stderr, "Usage: %s seconds nanoseconds\n", argv[0]); 
        return EXIT_FAILURE;
    }

    int durationSec = atoi(argv[1]);
    int durationNano = atoi(argv[2]);

    key_t shm_key = ftok("oss.c", 0);
    if (shm_key == (key_t)-1) {
        fprintf(stderr,"OSC: Error in ftok\n"); 
        return EXIT_FAILURE;
    }

    int shm_id = shmget(shm_key, BUFF_SZ, 0700); // we remove IPC_CREAT here because we assume oss has already created the shared memory segment. 
    //Day 3's task is to make sure worker can read the shared memory clock, so we assume oss has already set it up. 
    if (shm_id == -1) {
        fprintf(stderr,"OSC: Error in shmget\n");
        return EXIT_FAILURE;
    }

    int *clock = (int *)shmat(shm_id, NULL, 0);
    if (clock == (void *)-1) {
        fprintf(stderr,"OSC: Error in shmat\n");
        return EXIT_FAILURE;
    }

    // // init clock values to 0,0

    // int *sec  = &(clock[0]);
    // int *nano = &(clock[1]);
    // *sec = 0;
    // *nano = 0;
    //we comment out the above code because we assume oss has already initialized the clock values to 0,0.


    printf("WORKER PID:%d PPID:%d\n", getpid(), getppid());
    printf("WORKER: Received arguments: seconds=%d nanoseconds=%d\n", durationSec, durationNano);
    printf("WORKER: Read shared memory clock: SysClockS=%d SysclockNano=%d\n", clock[0], clock[1]);

    key_t msg_key = ftok("oss.c", 1);
    if (msg_key == (key_t)-1) {
        perror("WORKER: Error in ftok for message queue");
        shmdt(clock); // Detach from shared memory
        return EXIT_FAILURE;
    }

    int msg_id = msgget(msg_key, 0700); // we remove IPC_CREAT here because we assume oss has already created the message queue.
    if (msg_id == -1) {
        perror("WORKER: Error in msgget");
        shmdt(clock); // Detach from shared memory
        return EXIT_FAILURE;
    }

    struct Message msg;

    printf("WORKER: Waiting to receive message from OSS...\n");
    if (msgrcv(msg_id, &msg, sizeof(struct Message) - sizeof(long), 1, 0) == -1) {
        perror("WORKER: msgrcv failed");
        shmdt(clock);
        return EXIT_FAILURE;
    }

    printf("WORKER: Received message from OSS with status: %d\n", msg.status);
    printf("WORKER: Clock after message received: SysClockS=%d SysclockNano=%d\n", clock[0], clock[1]);

    struct Message reply;
    reply.mtype = 2; 
    reply.status = 1; // we can use this field to indicate the status of the message, but we will just set it to 1 for now to indicate that the worker has processed the message.

    if (msgsnd(msg_id, &reply, sizeof(struct Message) - sizeof(long), 0) == -1) {
        perror("WORKER: msgsnd failed");
        shmdt(clock);
        return EXIT_FAILURE;
    }

    printf("WORKER: Sent message back to oss.\n");

    shmdt(clock); // Detach from shared memory
    return EXIT_SUCCESS;
}
