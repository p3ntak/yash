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
int pid_ch1 = 99, pid_ch2 = 99, pid = 99;
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
//                    removeFromJobs(jobs, pid_ch1, pactiveJobsSize);
                    count++;
                } else if(WIFSIGNALED(status))
                {
                    count++;
                } else if(WIFSTOPPED(status))
                {
                    setJobStatus(jobs, pid_ch1, activeJobsSize, STOPPED);
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
    signal(SIGINT, sig_int);
    kill(pid_ch1, SIGINT);
}


static void sig_tstp(int signo)
{
    signal(SIGTSTP, sig_tstp);
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
