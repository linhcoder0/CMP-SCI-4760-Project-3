#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>

#define MAX_PCB_SIZE 20

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
    printf("oss: n=%d s=%d t=%f i=%f\n f=%s", n, s, t, i, logFile);

    return EXIT_SUCCESS;
}