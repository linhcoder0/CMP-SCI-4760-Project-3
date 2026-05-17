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
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
//• Implement the simultaneous restriction, as well as implement the process table and store data in it. [Day 8-9]



#define MAX_PCB_SIZE 20
#define NANOPERSEC 1000000000 //1 second = 1 billion nanoseconds
#define PRINT_INTERVAL_NS 500000000LL //.5 seconds = 500 million nanoseconds

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

const size_t BUFF_SZ = sizeof(int) * 2;

static volatile sig_atomic_t shutdownFlag = 0;
static volatile sig_atomic_t shutdownSig = 0;

static int shm_id_global = -1;
static int msg_id_global = -1;
static int *clock_global = NULL;

void signal_handler(int sig) {
    shutdownFlag = 1;
    shutdownSig = sig;
}

void logBoth(FILE *logFile, const char *format, ...) { 
    va_list args;

    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    if (logFile != NULL) {
        va_start(args, format);
        vfprintf(logFile, format, args);
        va_end(args);
        fflush(logFile);
    }

    fflush(stdout);
}

void clearPCBEntry(int slot) {
    pcbTable[slot].occupied = 0;
    pcbTable[slot].pid = 0;
    pcbTable[slot].startSeconds = 0;
    pcbTable[slot].startNano = 0;
    pcbTable[slot].endingTimeSeconds = 0;
    pcbTable[slot].endingTimeNano = 0;
    pcbTable[slot].messagesSent = 0;
}

void printProcessTable(FILE *logFile, int *clock) {
    logBoth(logFile, "\nOSS PID:%d SysClockS: %d SysclockNano: %d\n",
            getpid(), clock[0], clock[1]);

    logBoth(logFile, "Process Table:\n");
    logBoth(logFile, "Entry Occupied PID StartS StartN EndingTS EndingTN MessagesSent\n");

    for (int i = 0; i < MAX_PCB_SIZE; i++) {
        logBoth(logFile, "%2d %8d %6d %6d %6d %8d %8d %12d\n",
                i,
                pcbTable[i].occupied,
                (int)pcbTable[i].pid,
                pcbTable[i].startSeconds,
                pcbTable[i].startNano,
                pcbTable[i].endingTimeSeconds,
                pcbTable[i].endingTimeNano,
                pcbTable[i].messagesSent);
    }
}


void cleanupIPC(void) {
    // Detach from shared memory if attached
    if (clock_global != NULL && clock_global != (void *)-1) {
        shmdt(clock_global);
        clock_global = NULL;
    }

    // Mark shared memory segment for deletion
    if (shm_id_global != -1) {
        shmctl(shm_id_global, IPC_RMID, NULL);
        shm_id_global = -1;
    }

    // Mark message queue for deletion
    if (msg_id_global != -1) {
        msgctl(msg_id_global, IPC_RMID, NULL);
        msg_id_global = -1;
    }
}


struct Message {
    long mtype; // message type, must be > 0
    int status; // 1 = running, 0 = finished
    //we added pid and slot to message structure in both oss.c and worker.c because
    //we want the worker process to be able to send a message back to the oss process
    //with its pid and slot number in the PCB table, so that oss can update the PCB table accordingly when it receives the message from the worker process.
    int pid; 
    int slot;
};

void killRunningChildren(void) {
    for (int i = 0; i < MAX_PCB_SIZE; i++) {
        if (pcbTable[i].occupied == 1 && pcbTable[i].pid > 0) {
            kill(pcbTable[i].pid, SIGTERM);
        }
    }
}


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
    if (s > MAX_PCB_SIZE) s = MAX_PCB_SIZE; //we cannot have more simul processes than the size of the PCB table

    FILE *logFilePtr = fopen(logFile, "w"); //open the log file for writing
    if (logFilePtr == NULL) {
        perror("OSS: Error opening log file");
        return EXIT_FAILURE;
    }

    srand((unsigned int)time(NULL));

    for (int i = 0; i < MAX_PCB_SIZE; i++) { //init the PCB table by setting all entries to unoccupied and pid to 0
        clearPCBEntry(i);
    }

    
    logBoth(logFilePtr, "OSS: starting, PID:%d PPID:%d\nCalled With: \n", getpid(), getppid()); 
    logBoth(logFilePtr, "OSS: n=%d s=%d t=%f i=%f f=%s\n", n, s, t, i, logFile); 

    //Implement oss initialization of shared memory and worker being able to take in arguments. At this stage, just make sure
    //that worker can read the shared memory clock. [Day 3]
    key_t shm_key = ftok("oss.c", 0);
    if (shm_key == (key_t)-1) {
        fprintf(stderr,"OSS: Error in ftok\n"); 
        fclose(logFilePtr);
        return EXIT_FAILURE;
    }

    int shm_id = shmget(shm_key, BUFF_SZ, IPC_CREAT | 0700);
    if (shm_id == -1) {
        fprintf(stderr,"OSS: Error in shmget\n");
        fclose(logFilePtr);
        return EXIT_FAILURE;
    }

    int *clock = (int *)shmat(shm_id, NULL, 0);
    if (clock == (void *)-1) {
        fprintf(stderr,"OSS: Error in shmat\n");
        shmctl(shm_id, IPC_RMID, NULL); // Mark the shared memory segment for deletion
        fclose(logFilePtr);
        return EXIT_FAILURE;
    }

    shm_id_global = shm_id; 
    clock_global = clock;

    // init clock values to 0,0 
    int *sec  = &(clock[0]);
    int *nano = &(clock[1]);

    *sec = 0;
    *nano = 0;

    // printf("OSS: initialized shared memory clock: sec=%d nano=%d\n", clock[0], clock[1]);

    //set up message queue for communication between oss and worker processes
    key_t msg_key = ftok("oss.c", 1);
    if (msg_key == (key_t)-1) { 
        fprintf(stderr,"OSS: Error in ftok for message queue\n"); 
        cleanupIPC(); // Detach from shared memory and mark shared memory and message queue for deletion
        fclose(logFilePtr);
        return EXIT_FAILURE;
    }

    int msg_id = msgget(msg_key, IPC_CREAT | 0700); 
    if (msg_id == -1) {
        fprintf(stderr,"OSS: Error in msgget\n");
        cleanupIPC(); // Detach from shared memory and mark shared memory and message queue for deletion
        fclose(logFilePtr);
        return EXIT_FAILURE;
    }

    msg_id_global = msg_id;

    logBoth(logFilePtr, "OSS: initialized shared memory clock and message queue with id: %d\n", msg_id);


    int launchedChildren = 0;
    //we will use this variable to keep track of how many child processes we have launched so far. We will use this to make sure we do not launch more than n child processes.
    int runningChildren = 0;
    int finishedChildren = 0;
    //we will use this variable to keep track of how many child processes have finished so far. We will use this to make sure we wait for all child processes to finish before we clean up
    int totalMessagesSent = 0;
    int status = 0; 
    int nextMessageSlot = 0;
    long long nextPrintNS = PRINT_INTERVAL_NS; // we will use this variable to keep track of when we should print the process table next. We will print the process table every 0.5 seconds, which is 500 million nanoseconds.

    int launchIntervalSec = (int)i; // we will use this variable to keep track of the interval at which we should launch new child processes. We will launch a new child process every t seconds.
    int launchIntervalNano = (int)((i - launchIntervalSec) * NANOPERSEC); // we will use this variable to keep track of the nanosecond part of the launch interval.

    int lastLaunchSec = 0; 
    int lastLaunchNano = 0;
    int launchedAtLeastOne = 0;
    
    while(!shutdownFlag && (launchedChildren < n || runningChildren > 0)) { //we will keep looping until we have launched all n child processes and all child processes have finished, or until we receive a shutdown signal
        //"oss to increment the clock by 250ms divided by the number of current children."
        if(runningChildren > 0) {
            *nano += 250000000 / runningChildren;
        } else {
            *nano += 10000000;
        }

        while (*nano >= NANOPERSEC) {
            *nano -= NANOPERSEC;
            (*sec)++;
        }

        long long currentTimeNS = (long long)(*sec) * NANOPERSEC + *nano; 

        if (currentTimeNS >= nextPrintNS) {
            printProcessTable(logFilePtr, clock);

            while(nextPrintNS <= currentTimeNS){
            nextPrintNS += PRINT_INTERVAL_NS;
            }
        }

        while(launchedChildren < n && runningChildren < s){
            int greenLight = 0;
            if(!launchedAtLeastOne) {
                greenLight = 1;
            } else if (launchIntervalSec == 0 && launchIntervalNano == 0) {
                greenLight = 1;
            } else {
                long long lastLaunchNS = ((long long)lastLaunchSec * NANOPERSEC) + lastLaunchNano;
                long long intervalNS = ((long long)launchIntervalSec * NANOPERSEC) + launchIntervalNano;
                if (currentTimeNS - lastLaunchNS >= intervalNS) {
                    greenLight = 1;
                }
            }
            
            if(!greenLight) {
                break;
            }   

            //find the next available slot in the PCB table for the new child process
            int slot = -1;
            for (int i = 0; i < MAX_PCB_SIZE; i++) {
                if (pcbTable[i].occupied == 0) {
                    slot = i;
                    break;
                }
            }

            if (slot == -1) {
                logBoth(logFilePtr, "OSS: No available slot in PCB table for new child process\n");
                break; 
            }

            long long maxLifetimeNS = (long long) (t * NANOPERSEC);

            if (maxLifetimeNS < NANOPERSEC) {
                maxLifetimeNS = NANOPERSEC; 
                //make sure max lifetime of child process is at least 1 sec 
            }

            long long workerLifetimeNS = NANOPERSEC;

            if (maxLifetimeNS > NANOPERSEC) {
                long long range = maxLifetimeNS - NANOPERSEC;
                double r = (double)rand() / RAND_MAX;
                workerLifetimeNS += (long long)(r * range);
            }

            int workerSec = (int)(workerLifetimeNS / NANOPERSEC);
            int workerNano = (int)(workerLifetimeNS % NANOPERSEC);

            int estimatedRunningChildren = runningChildren + 1;
            long long estimatedEndTimeNS = currentTimeNS + (workerLifetimeNS * estimatedRunningChildren);

            int estimatedEndTimeSec = (int)(estimatedEndTimeNS / NANOPERSEC);
            int estimatedEndTimeNano = (int)(estimatedEndTimeNS % NANOPERSEC);

            pcbTable[slot].occupied = 1;
            pcbTable[slot].pid = 0;
            pcbTable[slot].startSeconds = *sec;
            pcbTable[slot].startNano = *nano;
            pcbTable[slot].endingTimeSeconds = estimatedEndTimeSec;
            pcbTable[slot].endingTimeNano = estimatedEndTimeNano;
            pcbTable[slot].messagesSent = 0;
            
            pid_t pid = fork();
            if (pid == -1) { //if there is something inside in the child process, then we will have an error in fork
                fprintf(stderr, "OSS: Error in fork\n");
                clearPCBEntry(slot); // Detach from shared memory and mark shared memory and message queue for deletion
                shutdownFlag = 1; // set shutdown flag to true to break out of the main loop and clean up
                break;
            }

            if(pid == 0) { //if pid is 0, then we are in the child process, which is the worker process. We will execute the worker program using execl
                char secStr[16];
                char nanoStr[16];
                snprintf(secStr, sizeof(secStr), "%d", workerSec);
                snprintf(nanoStr, sizeof(nanoStr), "%d", workerNano);
                execl("./worker", "worker", secStr, nanoStr, (char *)NULL); 
                perror("OSS: execl failed");
                exit(EXIT_FAILURE);
            }

            pcbTable[slot].pid = pid; //update the PCB table with the pid of the new child process
            launchedChildren++;
            runningChildren++;

            lastLaunchSec = *sec;
            lastLaunchNano = *nano;
            launchedAtLeastOne = 1;

            logBoth(logFilePtr, 
                "OSS: Launched worker %d PID: %d, PCB Slot: %d, Time: %d:%d, Estimated Lifetime: %d sec %d nano, Estimated End Time: %d:%d\n",
                launchedChildren, pid, slot, *sec, *nano, workerSec, workerNano, estimatedEndTimeSec, estimatedEndTimeNano);

            printProcessTable(logFilePtr, clock);
            
            if (launchIntervalSec !=0 || launchIntervalNano != 0) {
                break;
            }
        }

        if (runningChildren == 0){
            continue;
        }

        //now we need to find the next occupied slot in the PCB table 

        int slottoMessage = -1;
        for (int i = 0; i < MAX_PCB_SIZE; i++) {
            int checkSlot = (nextMessageSlot + i) % MAX_PCB_SIZE;
            if (pcbTable[checkSlot].occupied == 1) {
                slottoMessage = checkSlot;
                nextMessageSlot = (checkSlot + 1) % MAX_PCB_SIZE; 
                break;
            }
        }

        if(slottoMessage == -1) {
            logBoth(logFilePtr, "OSS: No occupied slot in PCB table to send message to\n");
            continue; 
        }

        //now we need to send a message to one specific worker
        struct Message msg;
        msg.mtype = pcbTable[slottoMessage].pid; // we will use the pid of the worker process as the message type, so that only that worker process will receive the message
        msg.status = 1; // we can use this field to indicate the status of the
        msg.pid = getpid();
        msg.slot = slottoMessage;

        logBoth(logFilePtr, "OSS: Sending message to worker slot: %d PID: %d at time: %d:%d\n", slottoMessage, pcbTable[slottoMessage].pid, *sec, *nano);
        if (msgsnd(msg_id, &msg, sizeof(struct Message) - sizeof(long), 0) == -1) {
            perror("OSS: msgsnd failed");
            shutdownFlag = 1; // set shutdown flag to true to break out of the main loop and clean up
            break;; 
        }

        pcbTable[slottoMessage].messagesSent++;
        totalMessagesSent++;

        struct Message reply;
        if (msgrcv(msg_id, &reply, sizeof(struct Message) - sizeof(long), 1, 0) == -1) {
            perror("OSS: msgrcv failed");
            shutdownFlag = 1; // set shutdown flag to true to break out of the main loop and clean up
            break;
        }

        logBoth(logFilePtr, "OSS: Received reply from worker slot: %d PID: %d with status: %d at time: %d:%d\n", reply.slot, pcbTable[reply.slot].pid, reply.status, *sec, *nano);

        if(reply.status == 0) {
            logBoth(logFilePtr, "OSS: Worker slot: %d PID: %d is terminated\n", reply.slot, pcbTable[reply.slot].pid);
            waitpid(pcbTable[reply.slot].pid, &status, 0); 
            clearPCBEntry(reply.slot);
            runningChildren--;
            finishedChildren++; 
        }
    }

    if(shutdownFlag) {
        logBoth(logFilePtr, "OSS: Received shutdown signal %d, shutting down...\n", shutdownSig);
        killRunningChildren(); // kill all running child processes
        while (wait(NULL) > 0); // wait for all child processes to finish
    }

    logBoth(logFilePtr, "OSS: Summary: Launched %d workers, finished %d workers, total messages sent: %d\n", launchedChildren, finishedChildren, totalMessagesSent);
    cleanupIPC(); // Detach from shared memory and mark shared memory and message queue for deletion
    fclose(logFilePtr);
    return EXIT_SUCCESS;
}


//     while(launchedChildren < n) {
//     pid_t pid = fork();

//     if (pid == -1) { //if there is something inside in the child process, then we will have an error in fork
//         fprintf(stderr, "OSS: Error in fork\n");
//         shmdt(clock); // Detach from shared memory
//         shmctl(shm_id, IPC_RMID, NULL); // Mark the shared memory segment for deletion
//         msgctl(msg_id, IPC_RMID, NULL); // Mark the message queue for deletion
//         return EXIT_FAILURE;
//     }

//     if (pid == 0) { //if pid is 0, then we are in the child process, which is the worker process. We will execute the worker program using execl
//         execl("./worker", "worker", "5", "500000", (char *)NULL); //right now we are just passing in some dummy arguments for the worker process, which is 5 seconds and 500000 nanoseconds. 
//         //We will change this later to pass in the actual arguments from the command line. Right now we just prove that worker can receive arguments and read the shared memory clock.
//         perror("OSS: execl failed");
//         exit(EXIT_FAILURE);
//     }

//     launchedChildren++;

//     printf("OSS: launched worker %d with PID: %d\n", launchedChildren, pid);

//     //Parent: send a msg to worker
//     struct Message msg;
//     msg.mtype = 1; // we can use any positive number for mtype, but we will just use 1 for simplicity. 
//     msg.status = 1; 

//     printf("OSS: sending message to worker PID: %d\n", pid);

//     if (msgsnd(msg_id, &msg, sizeof(struct Message) - sizeof(long), 0) == -1) {
//         perror("OSS: msgsnd failed"); // if we fail to send a message to the worker process, we will kill the worker process 
//         //and clean up the shared memory and message queue before exiting.
//         kill(pid, SIGKILL); 
//         wait(NULL); // wait for the child process to finish
//         shmdt(clock); // Detach from shared memory
//         shmctl(shm_id, IPC_RMID, NULL); // Mark the shared memory segment for deletion
//         msgctl(msg_id, IPC_RMID, NULL); // Mark the message queue for deletion
//         return EXIT_FAILURE;
//     }

//     //Parent: wait for a message / reply from worker
//     struct Message reply;

//     if (msgrcv(msg_id, &reply, sizeof(struct Message) - sizeof(long), 2, 0) == -1) { // we will use mtype 2 for the reply message from the worker process
//         perror("OSS: msgrcv failed"); // if we fail to receive a message from the worker process, we will kill the child process 
//         //and clean up the shared memory and message queue before exiting with failure
//         kill(pid, SIGKILL); 
//         wait(NULL); // wait for the child process to finish
//         shmdt(clock); // Detach from shared memory
//         shmctl(shm_id, IPC_RMID, NULL); // Mark the shared memory segment for deletion
//         msgctl(msg_id, IPC_RMID, NULL); // Mark the message queue for deletion
//         return EXIT_FAILURE;
//     }

//     printf("OSS: received reply from worker PID: %d with status: %d\n", pid, reply.status);

//     wait(NULL); // Wait for the child process to finish
//     finishedChildren++;
//     printf("OSS: worker PID: %d has finished. Total finished children: %d\n", pid, finishedChildren);
// }
//     shmdt(clock); // Detach from shared memory
//     shmctl(shm_id, IPC_RMID, NULL); // Mark the shared memory segment for deletion
//     msgctl(msg_id, IPC_RMID, NULL); // Mark the message queue for deletion

//     printf("OSS: Summary: Launched %d workers, finished %d workers.\n", launchedChildren, finishedChildren);

//     return EXIT_SUCCESS;
// }

