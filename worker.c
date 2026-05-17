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

#define NANOPERSEC 1000000000 //1 second = 1 billion nanoseconds

const size_t BUFF_SZ = sizeof(int) * 2;

struct Message {
    long mtype; // message type, must be > 0
    int status; // 1 = running, 0 = finished
    int pid; 
    int slot;
};

int main (int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "WORKER: Usage: %s seconds nanoseconds\n", argv[0]); 
        return EXIT_FAILURE;
    }

    int durationSec = atoi(argv[1]);
    int durationNano = atoi(argv[2]);

    if (durationSec < 0) durationSec = 0;
    if (durationNano < 0) durationNano = 0;

    while (durationNano >= NANOPERSEC) {
        durationSec++;
        durationNano -= NANOPERSEC;
    }

    key_t shm_key = ftok("oss.c", 0);
    if (shm_key == (key_t)-1) {
        fprintf(stderr,"WORKER: Error in ftok\n"); 
        return EXIT_FAILURE;
    }

    int shm_id = shmget(shm_key, BUFF_SZ, 0700); // we remove IPC_CREAT here because we assume oss has already created the shared memory segment. 
    //Day 3's task is to make sure worker can read the shared memory clock, so we assume oss has already set it up. 
    if (shm_id == -1) {
        fprintf(stderr,"WORKER: Error in shmget\n");
        return EXIT_FAILURE;
    }

    int *clock = (int *)shmat(shm_id, NULL, 0);
    if (clock == (void *)-1) {
        fprintf(stderr,"WORKER: Error in shmat\n");
        return EXIT_FAILURE;
    }

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

    int terminateSec = clock[0] + durationSec;
    int terminateNano = clock[1] + durationNano;

    while (terminateNano >= NANOPERSEC) {
        terminateSec++;
        terminateNano -= NANOPERSEC;
    }

    printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %d\n",
        getpid(), getppid(), clock[0], clock[1]);
    printf("TermTimeS: %d TermTimeNano: %d\n", terminateSec, terminateNano);
    printf("--Just Starting\n");

    fflush(stdout);

    int messagesReceived = 0;
    int done = 0;

    while (!done){
        struct Message msg;
        if(msgrcv(msg_id, &msg, sizeof(struct Message) - sizeof(long), getpid(), 0) == -1) {
            perror("WORKER: msgrcv failed");
            shmdt(clock); // Detach from shared memory
            return EXIT_FAILURE;
        }
    

    messagesReceived++;
    int currentSec = clock[0];
    int currentNano = clock[1];

    if (currentSec > terminateSec || (currentSec == terminateSec && currentNano >= terminateNano)) {
        done = 1;
    }

    printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %d\n",
        getpid(), getppid(), currentSec, currentNano);
    printf("TermTimeS: %d TermTimeNano: %d\n", terminateSec, terminateNano);

    if (done) {
        printf("--Terminating after sending message back to oss after %d received messages.\n",
            messagesReceived);
    } else {
        printf("--%d messages received from oss\n", messagesReceived);
    }

    fflush(stdout);

    struct Message reply;
    reply.mtype = 1; 
    if (done) {
        reply.status = 0; //terminate
    } else {
        reply.status = 1; //keep running
    }

    reply.pid = getpid();
    reply.slot = msg.slot;
    if(msgsnd(msg_id, &reply, sizeof(struct Message) - sizeof(long), 0) == -1){
        perror("WORKER: msgsnd failed");
        shmdt(clock); // Detach from shared memory
        return EXIT_FAILURE;
    }
    }
    shmdt(clock);

    return EXIT_SUCCESS;

    }

