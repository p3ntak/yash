#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include "helpers.h"
#include <fcntl.h>

//function declarations
int executeLine(char **args, char *line);
void mainLoop(void);
int startPipedOperation(char **args1, char **args2);
int startOperation(char **args);
int startBgOperation(char **args);
static void sig_int(int signo);
static void sig_tstp(int signo);
static void sig_handler(int signo);

// Global Vars
int pid_ch1 = -1, pid_ch2 = -1, pid = -1;
int activeJobsSize; //goes up and down as jobs finish
struct Job *jobs;
int *pactiveJobsSize = &activeJobsSize;

//main to take arguments and start a loop
int main(int argc, char **argv)
{
    jobs = malloc(sizeof(struct Job) * MAX_NUMBER_JOBS);

    mainLoop();

    free(jobs);
    return EXIT_SUCCESS;
}


void mainLoop(void)
{
    int status = 1;
    char *line;
    char **args;
    activeJobsSize = 0;
    signal(SIGINT, sig_int);
    signal(SIGTSTP, sig_tstp);
    signal(SIGCHLD, proc_exit);
    //read input line
    //parse input
    //stay in loop until an exit is requested
    //while waiting for user input SIGINT is ignored so ctrl+c will not stop the shell
    do
    {
        // ignore sigint and sigtstp while waiting for input
        printf("# ");
        line = readLineIn();
        if(line == NULL)
        {
            printf("\n");
            killProcs(jobs, pactiveJobsSize);
            break;
        }
        if(strcmp(line,"") == 0) continue;
        char *lineCpy = strdup(line);
        args = parseLine(line);
        status = executeLine(args, lineCpy);
        printf("\n");
    } while(status);
    return;
}


int executeLine(char **args, char *line)
{
    if(!*args) return FINISHED_INPUT;
    int returnVal;
    int inBackground = containsAmp(args);   //check if args contains '&'
    int inputPiped = pipeQty(args);         //get number of pipes in the command

    if(!(
            (strcmp(args[0], BUILT_IN_BG) == 0) ||
            (strcmp(args[0], BUILT_IN_FG) == 0) ||
            (strcmp(args[0], BUILT_IN_JOBS) == 0)))
    {
        addToJobs(jobs, line, pactiveJobsSize);
    }

    // check if command is a built in command
    if(strcmp(args[0], BUILT_IN_BG) == 0)
    {
        yash_bg(jobs, activeJobsSize);
        return FINISHED_INPUT;
    }
    if(strcmp(args[0], BUILT_IN_FG) == 0)
    {
        yash_fg(jobs, activeJobsSize, pactiveJobsSize);
        return FINISHED_INPUT;
    }
    if(strcmp(args[0], BUILT_IN_JOBS) == 0)
        return yash_jobs(jobs, activeJobsSize);

    //make sure & and | are not both in the argument
    if(!pipeBGExclusive(args))
    {
        printf("Cannot background and pipeline commands "
                       "('&' and '|' must be used separately).");
        return FINISHED_INPUT;
    }

    // if there are more than 1 or less than 0 pipes reject the input as it is not a valid command
    if (inputPiped > 1 || inputPiped < 0)
    {
        printf("Only one '|' allowed per line");
        return FINISHED_INPUT;
    }

    //if there is a | in the argument then
    if(inputPiped == 1)
    {
        struct PipedArgs pipedArgs = getTwoArgs(args);
        returnVal = startPipedOperation(pipedArgs.args1, pipedArgs.args2);
        free(pipedArgs.args1);
        free(pipedArgs.args2);
        free(args);
        return returnVal;
    }

    if(inBackground)
    {
        returnVal = startBgOperation(args);
    } else
    {
        returnVal = startOperation(args);
    }
    free(args);
    return returnVal;
}

int startBgOperation(char **args)
{
    removeAmp(args);
    FILE *writeFilePointer = NULL;
    FILE *readFilePointer = NULL;
    int argCount = countArgs(args);
    int redirIn = containsInRedir(args);
    int redirOut = containsOutRedir(args);
    int fd = open("/dev/null", O_WRONLY);

    pid_ch1 = fork();
    if (pid_ch1 == 0)
    {
        if (redirOut >= 0)
        {
            if(setRedirOut(args, redirOut, writeFilePointer, argCount) == -1)
            {
                removeLastFromJobs(jobs, pactiveJobsSize);
                _exit(EXIT_FAILURE);
                return FINISHED_INPUT;
            }
        }

        if (redirIn >= 0)
        {
            if(setRedirIn(args, redirIn, readFilePointer, argCount) == -1)
            {
                removeLastFromJobs(jobs, pactiveJobsSize);
                _exit(EXIT_FAILURE);
                return FINISHED_INPUT;
            }
        }

        if((redirIn < 0 && redirOut <0) || (redirIn >= 0 && redirOut <0))
            dup2(fd, STDOUT_FILENO);
        if(execvp(args[0], args) == -1)
        {
            perror("Problem executing command");
            removeLastFromJobs(jobs, pactiveJobsSize);
            _Exit(EXIT_FAILURE);
        }

    } else if (pid_ch1 < 0)
    {
        perror("error forking");
    } else if (pid_ch1 > 0)
    {
        startJobsPID(jobs, pid_ch1, activeJobsSize);
    }
    if(writeFilePointer != NULL) fclose(writeFilePointer);
    if(readFilePointer != NULL) fclose(readFilePointer);
    return FINISHED_INPUT;
}

int startOperation(char **args)
{
    int status;
    removeAmp(args);
    FILE *writeFilePointer = NULL;
    FILE *readFilePointer = NULL;
    int argCount = countArgs(args);
    int redirIn = containsInRedir(args);
    int redirOut = containsOutRedir(args);

    pid_ch1 = fork();
    if(pid_ch1 == 0)
    {
        // child process

        if (redirOut >= 0)
        {
            if(setRedirOut(args, redirOut, writeFilePointer, argCount) == -1)
            {
                removeLastFromJobs(jobs, pactiveJobsSize);
                _exit(EXIT_FAILURE);
                return FINISHED_INPUT;
            }
        }

        if (redirIn >= 0) {
            if (setRedirIn(args, redirIn, readFilePointer, argCount) == -1)
            {
                removeLastFromJobs(jobs, pactiveJobsSize);
                _exit(EXIT_FAILURE);
                return FINISHED_INPUT;
            }
        }

        if(execvp(args[0], args) == -1)
        {
            perror("Problem executing command");
            removeLastFromJobs(jobs, pactiveJobsSize);
            _Exit(EXIT_FAILURE);
        }
    } else if(pid_ch1 < 0)
    {
        perror("error forking");
    } else
    {
        // Parent process
        startJobsPID(jobs, pid_ch1, activeJobsSize);
        // change sig catchers back to not ignore signals
        pid = waitpid(-1, &status, WUNTRACED | WCONTINUED);
        if (pid == -1) {
            perror("waitpid");
        }
        if (WIFEXITED(status) | WIFSIGNALED(status)) {
            removeFromJobs(jobs, pid_ch1, pactiveJobsSize);
        } else if (WIFSTOPPED(status)) {
            setJobStatus(jobs, pid_ch1, activeJobsSize, STOPPED);
        }
    }
    if(writeFilePointer != NULL) fclose(writeFilePointer);
    if(readFilePointer != NULL) fclose(readFilePointer);
    return FINISHED_INPUT;
}


int startPipedOperation(char **args1, char **args2)
{
    int status;
    int pfd[2];
    FILE *writeFilePointer = NULL;
    FILE *readFilePointer = NULL;
    int argCount1 = countArgs(args1);
    int argCount2 = countArgs(args2);
    int redirIn1 = containsInRedir(args1);
    int redirIn2 = containsInRedir(args2);
    int redirOut1 = containsOutRedir(args1);
    int redirOut2 = containsOutRedir(args2);

    if (pipe(pfd) == -1)
    {
        perror("pipe");
        return FINISHED_INPUT;
    }

    pid_ch1 = fork();
    if(pid_ch1 > 0)
    {
        //parent
        pid_ch2 = fork();
        if(pid_ch2 > 0)
        {
            close(pfd[0]);
            close(pfd[1]);
            int count = 0;
            while(count<2)
            {
                pid = waitpid(-1, &status, WUNTRACED | WCONTINUED);
                startJobsPID(jobs, pid_ch1, activeJobsSize);
                if(pid == -1)
                {
                    perror("waitpid");
                    return FINISHED_INPUT;
                }
                if(WIFEXITED(status))
                {
                    removeFromJobs(jobs, pid_ch1, pactiveJobsSize);
                    count++;
                } else if(WIFSIGNALED(status))
                {
                    removeFromJobs(jobs, pid_ch1, pactiveJobsSize);
                    count++;
                } else if(WIFSTOPPED(status))
                {
                    setJobStatus(jobs, pid_ch1, activeJobsSize, STOPPED);
                    count++;
                    count++;
                } else if(WIFCONTINUED(status))
                {
                    setJobStatus(jobs, pid_ch1, activeJobsSize, RUNNING);
                    pid = waitpid(-1, &status, WUNTRACED | WCONTINUED);
                }
            }
            return FINISHED_INPUT;
        } else
        {
            // child 2
            sleep(1);
            setpgid(0, pid_ch1);
            close(pfd[1]);
            dup2(pfd[0],STDIN_FILENO);

            if (redirOut2 >= 0)
            {
                if(setRedirOut(args2, redirOut2, writeFilePointer, argCount2) == -1)
                {
                    removeLastFromJobs(jobs, pactiveJobsSize);
                    _exit(EXIT_FAILURE);
                    return FINISHED_INPUT;
                }
            }

            if (redirIn2 >= 0)
            {
                if(setRedirIn(args2, redirIn2, readFilePointer, argCount2) == -1)
                {
                    removeLastFromJobs(jobs, pactiveJobsSize);
                    _exit(EXIT_FAILURE);
                    return FINISHED_INPUT;
                }
            }

            if(execvp(args2[0], args2) == -1)
            {
                perror("Problem executing command 2");
                removeLastFromJobs(jobs, pactiveJobsSize);
                _Exit(EXIT_FAILURE);
                return FINISHED_INPUT;
            }
        }
    } else
    {
        // child 1
        setsid();
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        if (redirOut1 >= 0)
        {
            if(setRedirOut(args1, redirOut1, writeFilePointer, argCount1) == -1)
            {
                removeLastFromJobs(jobs, pactiveJobsSize);
                _exit(EXIT_FAILURE);
                return FINISHED_INPUT;
            }
        }

        if (redirIn1 >= 0)
        {
            if(setRedirIn(args1, redirIn1, readFilePointer, argCount1) == -1)
            {
                removeLastFromJobs(jobs, pactiveJobsSize);
                _exit(EXIT_FAILURE);
                return FINISHED_INPUT;
            }
        }

        if(execvp(args1[0], args1) == -1)
        {
            perror("Problem executing command 1");
            removeLastFromJobs(jobs, pactiveJobsSize);
            _Exit(EXIT_FAILURE);
            return FINISHED_INPUT;
        }
    }
    if(writeFilePointer != NULL) fclose(writeFilePointer);
    if(readFilePointer != NULL) fclose(readFilePointer);
    return FINISHED_INPUT;
}


static void sig_int(int signo)
{
    if(pid_ch1 == -1)
        return;
    signal(SIGINT, sig_int);
    kill(-pid_ch1, SIGINT);
}


static void sig_tstp(int signo)
{
    if(pid_ch1 == -1)
        return;
    signal(SIGTSTP, sig_tstp);
    kill(-pid_ch1, SIGTSTP);
    kill(pid_ch1, SIGTSTP);
}

static void proc_exit(int signo)
{
    int	sig_chld_pid;
    int status;
    while((sig_chld_pid = waitpid(-1, &status, WNOHANG))>0) {
        for (int i = 0; i < activeJobsSize; i++) {
            if (jobs[i].pid_no == sig_chld_pid)
                printf("\n[%d] DONE    %s\n", jobs[i].task_no, jobs[i].line);
        }
        printf("# ");
        fflush(stdout);
        removeFromJobs(jobs, sig_chld_pid, pactiveJobsSize);
    }
    return;
}

void fg_handler(int signo)
{
    pid_t	sig_chld_pid;

    sig_chld_pid = wait(NULL);
    removeFromJobs(jobs, sig_chld_pid, pactiveJobsSize);
}

static void sig_handler(int signo) {
    switch(signo){
        case SIGINT:
            signal(signo,SIG_IGN);
            signal(SIGINT,sig_handler);
            break;
        case SIGTSTP:
            signal(signo,SIG_IGN);
            signal(SIGTSTP,sig_handler);
            break;
        case SIGCHLD:
            signal(signo,SIG_IGN);
            signal(SIGCHLD, sig_handler);
            break;
        default:
            return;
    }

}

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

    if(activeJobSize == 0)
    {
        printf("yash: No active jobs");
        return;
    }

    pid_ch1 = jobs[activeJobSize - 1].pid_no;
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
            if(jobs[i].pid_no == pid_ch1)
                printf("[%d] + %s    %s\n", jobs[i].task_no, runningStr , jobs[i].line);

        } else
        {
            if(jobs[i].pid_no == pid_ch1)
                printf("[%d] - %s    %s\n", jobs[i].task_no, runningStr, jobs[i].line);
        }
    }
    char *line_cpy = strdup(jobs[activeJobSize -1].line);
    if(pipeQty(parseLine(line_cpy)) > 0) {
        kill(-pid_ch1, SIGCONT);
    } else {
        kill(pid_ch1, SIGCONT);
    }
    pid = waitpid(-1, &status, WUNTRACED | WCONTINUED);
    fflush(stdout);
    if (WIFCONTINUED(status)) {
        pid = waitpid(-1, &status, WUNTRACED | WCONTINUED);
    }
    if (pid == -1) {
        perror("waitpid");
    }
    if (WIFEXITED(status) || WIFSIGNALED(status)) {
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
    int made_it_to_end = 1;

    if(activeJobSize == 0)
    {
        printf("yash: No active jobs");
        return;
    }
    for(int i=activeJobSize-1; i>=0; i--)
    {
        char *line_cpy = strdup(jobs[i].line);
        int num_pipes = pipeQty(parseLine(line_cpy));
        if((num_pipes == 0) && (jobs[i].runningStatus == STOPPED)) {
            pid = jobs[i].pid_no;
            made_it_to_end = 0;
            break;
        }
    }
    if(made_it_to_end == 1){
        printf("No jobs available to put in background.\n");
        return;
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
