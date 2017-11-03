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
    int is_foreground; //boolean
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

//returns how many '|' are in the arguments
int pipeQty(char **args)
{
    int pipeCount = 0;
    int numArgs = countArgs(args);
    for (int i=0; i<numArgs;i++)
    {
        if(strstr(args[i],"|") && strlen(args[i]) == 1) pipeCount++;
    }
    return pipeCount;
}

//returns 0 if pipe and & are not exclusive
int pipeBGExclusive(char **args)
{
    int pipeCount = 0;
    int backgroundCount = 0;
    int numArgs = countArgs(args);
    for (int i=0; i<numArgs; i++)
    {
        if(strstr(args[i],"|")) pipeCount++;
        if(strstr(args[i],"&")) backgroundCount++;
    }
    if((pipeCount>0) && (backgroundCount>0))
    {
        return 0;
    }
    return 1;
}

//determine how many arguments were given to input
int countArgs(char **args)
{
    if(!*args) return 0;
    int numArgs = 0;
    int i=0;
    while(args[i] != NULL)
    {
        numArgs++;
        i++;
    }
    return numArgs;
}

//read input until end of file or new line
char *readLineIn(void)
{
    char *line = calloc(MAX_INPUT_LENGTH + 1, sizeof(char));

    if(!line)
    {
        fprintf(stderr,"line in memory allocation error\n");
        exit(EXIT_FAILURE);
    }

    line = fgets(line,MAX_INPUT_LENGTH+1,stdin);
    if(line == NULL) {
        return NULL;
    }
    if(strcmp(line,"\n") == 0) return "";
    char *lineCopy = strdup(line);

    free(line);
    return lineCopy;
}

// the following line parser was taken from https://brennan.io/2015/01/16/write-a-shell-in-c/
#define LSH_TOK_BUFSIZE 64
#define LSH_TOK_DELIM " \t\r\n\a"
char **parseLine(char *line)
{
    int bufsize = LSH_TOK_BUFSIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token;

    if (!tokens) {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, LSH_TOK_DELIM);
    while (token != NULL) {
        tokens[position] = token;
        position++;

        if (position >= bufsize) {
            bufsize += LSH_TOK_BUFSIZE;
            tokens = realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) {
                fprintf(stderr, "lsh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, LSH_TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

// splits a piped argument into a struct containing two separate arguments
struct PipedArgs getTwoArgs(char **args)
{
    char **args1 = malloc(sizeof(char*) * MAX_INPUT_LENGTH);
    char **args2 = malloc(sizeof(char*) * MAX_INPUT_LENGTH);
    int numArgs = countArgs(args);
    int i = 0;
    int k = 0;
    while(!strstr(args[i],"|"))
    {
        args1[k] = args[i];
        i++;
        k++;
    };
    args1[k] = NULL;
    i++;
    int j = 0;
    while(i < numArgs)
    {
        args2[j] = args[i];
        i++;
        j++;
    };
    args2[j] = NULL;
    struct PipedArgs pipedArgs = {args1, args2};
    return pipedArgs;
}

// add a process to jobs table
void addToJobs(struct Job *jobs, char *line, int *activeJobsSize)
{
    jobs[*activeJobsSize].line = strdup(line);

    if(*activeJobsSize == 0)
        jobs[*activeJobsSize].task_no = 1;
    else
        jobs[*activeJobsSize].task_no = jobs[*activeJobsSize-1].task_no + 1;

    jobs[*activeJobsSize].runningStatus = STOPPED;
    jobs[*activeJobsSize].pid_no = 0;

    (*activeJobsSize)++;
    return;
}

// built in jobs command. prints out each job's pid number, jobs number, and status
int yash_jobs(struct Job *jobs, int activeJobsSize)
{
    for(int i=0; i<activeJobsSize; i++)
    {
        char *runningStr;

        if(jobs[i].runningStatus)
            runningStr = "Running";
        else
            runningStr = "Stopped";

        if(i == activeJobsSize-1)
            printf("[%d] + %s  %d  %s\n", jobs[i].task_no, runningStr ,jobs[i].pid_no ,jobs[i].line);
        else
            printf("[%d] - %s  %d  %s\n", jobs[i].task_no, runningStr, jobs[i].pid_no ,jobs[i].line);
    }
    if(activeJobsSize == 0) printf("No active jobs\n");
    return FINISHED_INPUT;
}

// built in fg command. puts the most recent command from the jobs table into the foreground
void yash_fg(struct Job *jobs, int activeJobSize, int *pActiveJobSize)
{
    int status;
    signal(SIGCONT, SIG_DFL);

    if(activeJobSize == 0)
    {
        printf("yash: No active jobs");
        return;
    }

    int pid = jobs[activeJobSize - 1].pid_no;
    setJobStatus(jobs, pid, activeJobSize, RUNNING);
    for(int i=0; i<activeJobSize; i++)
    {
        char *runningStr;
        if(jobs[i].runningStatus)
            runningStr = "Running";
        else
            runningStr = "Stopped";
        if(i == activeJobSize-1)
        {
            if(jobs[i].pid_no == pid)
            printf("[%d] + %s    %s\n", jobs[i].task_no, runningStr , jobs[i].line);

        } else
        {
            if(jobs[i].pid_no == pid)
            printf("[%d] - %s    %s\n", jobs[i].task_no, runningStr, jobs[i].line);
        }
    }
    kill(pid, SIGCONT);
    pid = waitpid(-1, &status, WUNTRACED | WCONTINUED);
    if (pid == -1) {
        perror("waitpid");
    }
    if (WIFEXITED(status)) {
        removeFromJobs(jobs, pid, pActiveJobSize);
    } else if (WIFSTOPPED(status)) {
        setJobStatus(jobs, pid, activeJobSize, STOPPED);
    }
    return;
}

// build in bg command. puts the most recent stopped job in the background
void yash_bg(struct Job *jobs, int activeJobSize)
{
    int pid=0;
    signal(SIGCONT, SIG_DFL);

    if(activeJobSize == 0)
    {
        printf("yash: No active jobs");
        return;
    }
    for(int i=activeJobSize-1; i>=0; i--)
    {
        if(pipeQty(parseLine(jobs[i].line)) != 0) continue;
        if(jobs[i].runningStatus == STOPPED)
        {
            pid = jobs[i].pid_no;
            break;
        }
    }
    setJobStatus(jobs, pid, activeJobSize, RUNNING);
    for(int i=0; i<activeJobSize; i++)
    {
        char *runningStr;

        if(jobs[i].runningStatus)
            runningStr = "Running";
        else
            runningStr = "Stopped";

        if(i == activeJobSize-1)
        {
            if(jobs[i].pid_no == pid)
                printf("[%d] + %s    %s\n", jobs[i].task_no, runningStr , jobs[i].line);

        } else
        {
            if(jobs[i].pid_no == pid)
                printf("[%d] - %s    %s\n", jobs[i].task_no, runningStr, jobs[i].line);
        }
    }
    kill(pid, SIGCONT);
    return;
}

// updates the jobs table with the pid number and gives the job a 'running' status
void startJobsPID(struct Job *jobs, int pid, int activeJobsSize)
{
    jobs[activeJobsSize-1].pid_no = pid;
    jobs[activeJobsSize-1].runningStatus = RUNNING;
    return;
}

// removes a job by pid number from the jobs table. should be used when a job is finished or killed.
void removeFromJobs(struct Job *jobs, int pid, int *activeJobsSize)
{
    for(int i=0; i<*activeJobsSize; i++)
    {
        if((jobs[i].pid_no == pid))
        {
            for(int j=i; j<(*activeJobsSize-1); j++)
            {
                jobs[j].pid_no = jobs[j+1].pid_no;
                jobs[j].runningStatus = jobs[j+1].runningStatus;
                jobs[j].task_no = jobs[j+1].task_no;
                jobs[j].line = strdup(jobs[j+1].line);
            }
            jobs[*activeJobsSize-1].pid_no = 0;
            jobs[*activeJobsSize-1].runningStatus = STOPPED;
            jobs[*activeJobsSize-1].task_no = 0;
            jobs[*activeJobsSize-1].line = NULL;
            (*activeJobsSize)--;
        }
    }
    return;
}

// changes the status of a job in the jobs table in the event that it is stopped or restarted
void setJobStatus(struct Job *jobs, int pid, int activeJobsSize, int runningStatus)
{
    for(int i=0; i<activeJobsSize; i++)
    {
        if(jobs[i].pid_no == pid) jobs[i].runningStatus = runningStatus;
    }
    return;
}

// kills all process in the jobs table in the event that the shell is killed with ctrl + d
void killProcs(struct Job *jobs, int *activeJobsSize)
{
    for(int i=0; i<*activeJobsSize; i++)
    {
        kill(-jobs[i].pid_no, SIGINT);
        removeFromJobs(jobs, jobs[i].pid_no, activeJobsSize);
        (*activeJobsSize)--;
    }
}

// checks if the input arguments contain an & as an argument
int containsAmp(char **args)
{
    int argCount = countArgs(args);
    for (int i=0; i<argCount; i++)
    {
        if(strstr(args[i],"&")) return 1;
    }
    return 0;
}

// removes the most recent job from the jobs table in the event that the job was put in the table but killed before
// the pid no was assigned
void removeLastFromJobs(struct Job *jobs, int *activeJobsSize)
{
    jobs[*activeJobsSize-1].pid_no = 0;
    jobs[*activeJobsSize-1].runningStatus = STOPPED;
    jobs[*activeJobsSize-1].task_no = 0;
    jobs[*activeJobsSize-1].line = NULL;

    (*activeJobsSize)--;
    return;
}

// removes the '&' from the input arguments so the path command can be executed
void removeAmp(char **args)
{
    int argCount = countArgs(args);
    if(strcmp(args[argCount-1],"&") == 0)
        args[argCount-1] = NULL;
    return;
}

// checks if input arguments have a '<' symbol and returns the index of the symbol in the args array
int containsInRedir(char **args)
{
    int symbolPos = -1; // return -1 if '<' is not in args
    int argCount = countArgs(args);

    for(int i=0; i<argCount; i++)
    {
        if(strcmp(args[i],"<") == 0)
            symbolPos = i;
    }

    return symbolPos;
}

// checks if input arguments have a '>' symbol and returns the index of the symbol in the args array
int containsOutRedir(char **args)
{
    int symbolPos = -1; // return -1 if '<' is not in args
    int argCount = countArgs(args);

    for(int i=0; i<argCount; i++)
    {
        if(strcmp(args[i],">") == 0)
            symbolPos = i;
    }

    return symbolPos;
}

// removes the redirect argument and the next argument in the array. assumes the next argument is the name of the file
// any additional input in the args command will not be recognized by path command
void removeRedirArgs(char **args, int redirIndex)
{
    args[redirIndex] = NULL;
    args[redirIndex + 1] = NULL;
    return;
}

// set stdout to go to file specified by the writeFilePointer
int setRedirOut(char **args, int redirOut, FILE *writeFilePointer, int argCount)
{
    if (redirOut + 1 < argCount)
    {
        writeFilePointer = fopen(args[redirOut + 1], "w+");
        if (writeFilePointer)
        {
            dup2(fileno(writeFilePointer), STDOUT_FILENO);
            removeRedirArgs(args, redirOut);
        } else
        {
            fprintf(stderr, "Cannot open file %s\n", args[redirOut + 1]);
            return -1; // return -1 as error value
        }
    } else
    {
        fprintf(stderr, "Invalid Expression\n");
        return -1; // return -1 as error value
    }

    return 1; // Finished without error
}

int setRedirIn(char **args, int redirIn, FILE *readFilePointer, int argCount)
{
    if (redirIn + 1 < argCount)
    {
        readFilePointer = fopen(args[redirIn + 1], "r");
        if (readFilePointer)
        {
            dup2(fileno(readFilePointer), STDIN_FILENO);
            removeRedirArgs(args, redirIn);
        } else
        {
            fprintf(stderr, "Cannot open file %s\n", args[redirIn + 1]);
            return -1;
        }
    } else
    {
        fprintf(stderr, "Invalid Expression\n");
        return -1;
    }

    return 1;
}

#endif //YASH_HELPERS_H
