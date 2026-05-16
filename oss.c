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

#define MAX_PCB_SIZE 20

const size_t BUFF_SZ = sizeof(int) * 2;

//• Write code for oss to parse options and receive the command parameters. [Day 2]

int main(int argc, char *argv[]) {
    int n = 1;  
    int s = 1;  
    float t = 1.0f; 
    float i = 0.0f;
    char logFile[256] = "log.txt"; 

    int opt;
    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) { //parse CL options using getopt
        switch (opt) {
            case 'h':
                printf("oss [-h] [-n proc] [-s simul] [-t iter] [-i interval] [-f logfile]\n");
                return 0;
            case 'n':
                n = atoi(optarg);
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 't':
                t = atof(optarg);
                break;
            case 'i':
                i = atof(optarg);
                break;
            case 'f':
                strncpy(logFile, optarg, sizeof(logFile) - 1);
                logFile[sizeof(logFile) - 1] = '\0'; 
                break;
            default:
                printf("Usage: %s [-h] [-n proc] [-s simul] [-t iter] [-i interval]\n", argv[0]);
                return 1;
        }
    }     
    //avoid invalid input
    if (n <= 0) n = 1;
    if (s <= 0) s = 1;
    if (t <= 0) t = 1;
    if (i < 0) i = 0;

    if (s > n) s = n; //we cannot have more simul processes than total processes  
    printf("OSS starting, PID:%d PPID:%d Called With: \n", getpid(), getppid());
    printf("oss: n=%d s=%d t=%f i=%f f=%s\n", n, s, t, i, logFile);

    //Implement oss initialization of shared memory and worker being able to take in arguments. At this stage, just make sure
    //that worker can read the shared memory clock. [Day 3]
    key_t shm_key = ftok("oss.c", 0);
    if (shm_key == (key_t)-1) {
        fprintf(stderr,"OSC: Error in ftok\n"); 
        return EXIT_FAILURE;
    }

    int shm_id = shmget(shm_key, BUFF_SZ, IPC_CREAT | 0700);
    if (shm_id == -1) {
        fprintf(stderr,"OSC: Error in shmget\n");
        return EXIT_FAILURE;
    }

    int *clock = (int *)shmat(shm_id, NULL, 0);
    if (clock == (void *)-1) {
        fprintf(stderr,"OSC: Error in shmat\n");
        return EXIT_FAILURE;
    }

    // init clock values to 0,0 
    int *sec  = &(clock[0]);
    int *nano = &(clock[1]);
    *sec = 0;
    *nano = 0;

    printf("OSS initialized shared memory clock: sec=%d nano=%d\n", clock[0], clock[1]);

    pid_t pid = fork();

    if (pid == -1) { //if there is something inside in the child process, then we will have an error in fork
        fprintf(stderr, "OSS: Error in fork\n");
        shmdt(clock); // Detach from shared memory
        shmctl(shm_id, IPC_RMID, NULL); 
        return EXIT_FAILURE;
    }

    if (pid == 0) { //if pid is 0, then we are in the child process, which is the worker process. We will execute the worker program using execl
        execl("./worker", "worker", "5", "500000", (char *)NULL); //right now we are just passing in some dummy arguments for the worker process, which is 5 seconds and 500000 nanoseconds. 
        //We will change this later to pass in the actual arguments from the command line. Right now we just prove that worker can receive arguments and read the shared memory clock.
        perror("OSS: execl failed");
        exit(EXIT_FAILURE);
    }

    wait(NULL); // Wait for the child process to finish
    shmdt(clock); // Detach from shared memory
    shmctl(shm_id, IPC_RMID, NULL); // Mark the shared memory segment for deletion

    return EXIT_SUCCESS;
}