#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <getopt.h> 
#include <unistd.h>
#include <signal.h>
#include <string.h> 
#include <errno.h>

const size_t BUFF_SZ = sizeof(int) * 2;

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
    printf("Received arguments: seconds=%d nanoseconds=%d\n", durationSec, durationNano);
    printf("Read shared memory clock: SysClockS=%d SysclockNano=%d\n", clock[0], clock[1]);

    shmdt(clock); // Detach from shared memory
    return EXIT_SUCCESS;
}
