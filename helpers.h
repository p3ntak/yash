//
// Created by matt on 9/7/17.
//

#ifndef YASH_HELPERS_H
#define YASH_HELPERS_H

//function declarations
struct PipedArgs
{
    char **args1;
    char **args2;
};
struct Job
{
    char *line;
    int pid_no;
    int runningStatus; //boolean
    int task_no;
};
char *readLineIn(void);
char **parseLine(char *line);
int countArgs(char **args);
int pipeQty(char **args);
int pipeBGExclusive(char **args);
struct PipedArgs getTwoArgs(char **args);
void yash_fg(struct Job *jobs, int activeJobSize, int *pActiveJobSize);
void yash_bg(struct Job *jobs, int activeJobSize);
int yash_jobs(struct Job *jobs, int activeJobsSize);
int containsInRedir(char **args);
int containsOutRedir(char **args);
void addToJobs(struct Job *jobs, char *line, int *activeJobsSize);
void startJobsPID(struct Job *jobs, int pid, int activeJobsSize);
void removeFromJobs(struct Job *jobs, int pid, int *activeJobsSize);
void setJobStatus(struct Job *jobs, int pid, int activeJobsSize, int runningStatus);
void killProcs(struct Job *jobs, int *activeJobsSize);
int containsAmp(char **args);
void removeLastFromJobs(struct Job *jobs, int *activeJobsSize);
void removeAmp(char **args);
void removeRedirArgs(char **args, int redirIndex);
void fg_handler(int signo);
static void proc_exit(int signo);
int setRedirIn(char **args, int redirIn, FILE *readFilePointer, int argCount);
int setRedirOut(char **args, int redirOut, FILE *writeFilePointer, int argCount);

//#include "helpers.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_INPUT_LENGTH 200
#define DELIMS " \n"
#define FINISHED_INPUT 1
#define BUILT_IN_FG "fg"
#define BUILT_IN_BG "bg"
#define BUILT_IN_JOBS "jobs"
#define MAX_NUMBER_JOBS 50
#define RUNNING 1
#define STOPPED 0

//global vars
int shell_pid;

#endif //YASH_HELPERS_H
