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

#define MAX_PCB_SIZE 20

const size_t BUFF_SZ = sizeof(int) * 2;

struct PCB {
    int occupied; // either true or false
    pid_t pid; // process id of this child
    int startSeconds; // time when it was forked/created
    int startNano; // time when it was forked /created
    int endingTimeSeconds; // estimated time it should end
    int endingTimeNano; // estimated time it should end
    int messagesSent; // total times oss sent a message to it
};

struct PCB pcbTable[MAX_PCB_SIZE]; // we will have a PCB table to keep track of all the child processes. We will have a maximum of 20 child processes at any time, so we will have a PCB table of size 20.

struct Message {
    long mtype; // message type, must be > 0
    int status; // we can use this field to indicate the status of the message
};

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
    printf("OSS: starting, PID:%d PPID:%d\nCalled With: \n", getpid(), getppid());
    printf("OSS: n=%d s=%d t=%f i=%f f=%s\n", n, s, t, i, logFile);

    //Implement oss initialization of shared memory and worker being able to take in arguments. At this stage, just make sure
    //that worker can read the shared memory clock. [Day 3]
    key_t shm_key = ftok("oss.c", 0);
    if (shm_key == (key_t)-1) {
        fprintf(stderr,"OSS: Error in ftok\n"); 
        return EXIT_FAILURE;
    }

    int shm_id = shmget(shm_key, BUFF_SZ, IPC_CREAT | 0700);
    if (shm_id == -1) {
        fprintf(stderr,"OSS: Error in shmget\n");
        return EXIT_FAILURE;
    }

    int *clock = (int *)shmat(shm_id, NULL, 0);
    if (clock == (void *)-1) {
        fprintf(stderr,"OSS: Error in shmat\n");
        return EXIT_FAILURE;
    }

    // init clock values to 0,0 
    int *sec  = &(clock[0]);
    int *nano = &(clock[1]);
    *sec = 0;
    *nano = 0;

    printf("OSS: initialized shared memory clock: sec=%d nano=%d\n", clock[0], clock[1]);

    key_t msg_key = ftok("oss.c", 1);
    if (msg_key == (key_t)-1) { 
        fprintf(stderr,"OSS: Error in ftok for message queue\n"); 
        shmdt(clock); // Detach from shared memory
        shmctl(shm_id, IPC_RMID, NULL); 
        return EXIT_FAILURE;
    }

    int msg_id = msgget(msg_key, IPC_CREAT | 0700); 
    if (msg_id == -1) {
        fprintf(stderr,"OSS: Error in msgget\n");
        shmdt(clock); // Detach from shared memory
        shmctl(shm_id, IPC_RMID, NULL); 
        return EXIT_FAILURE;
    }

    printf("OSS: initialized message queue with id: %d\n", msg_id);

    pid_t pid = fork();

    if (pid == -1) { //if there is something inside in the child process, then we will have an error in fork
        fprintf(stderr, "OSS: Error in fork\n");
        msgctl(msg_id, IPC_RMID, NULL); // Mark the message queue for deletion
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

    //Parent: send a msg to worker
    struct Message msg;
    msg.mtype = 1; // we can use any positive number for mtype, but we will just use 1 for simplicity. 
    msg.status = 1; 

    printf("OSS: sending message to worker PID:: %d\n", pid);

    if (msgsnd(msg_id, &msg, sizeof(struct Message) - sizeof(long), 0) == -1) {
        perror("OSS: msgsnd failed"); // if we fail to send a message to the worker process, we will kill the worker process 
        //and clean up the shared memory and message queue before exiting.
        kill(pid, SIGKILL); 
        wait(NULL); // wait for the child process to finish
        shmctl(shm_id, IPC_RMID, NULL); // Mark the shared memory segment for deletion
        shmdt(clock); // Detach from shared memory
        msgctl(msg_id, IPC_RMID, NULL); // Mark the message queue for deletion
        return EXIT_FAILURE;
    }

    //Parent: wait for a message / reply from worker
    struct Message reply;

    if (msgrcv(msg_id, &reply, sizeof(struct Message) - sizeof(long), 2, 0) == -1) { // we will use mtype 2 for the reply message from the worker process
        perror("OSS: msgrcv failed"); // if we fail to receive a message from the worker process, we will kill the child process 
        //and clean up the shared memory and message queue before exiting with failure
        kill(pid, SIGKILL); 
        wait(NULL); // wait for the child process to finish
        shmctl(shm_id, IPC_RMID, NULL); // Mark the shared memory segment for deletion
        shmdt(clock); // Detach from shared memory
        msgctl(msg_id, IPC_RMID, NULL); // Mark the message queue for deletion
        return EXIT_FAILURE;
    }

    printf("OSS: received reply from worker PID: %d with status: %d\n", pid, reply.status);

    wait(NULL); // Wait for the child process to finish
    shmctl(shm_id, IPC_RMID, NULL); // Mark the shared memory segment for deletion
    shmdt(clock); // Detach from shared memory
    msgctl(msg_id, IPC_RMID, NULL); // Mark the message queue for deletion
    return EXIT_SUCCESS;
}