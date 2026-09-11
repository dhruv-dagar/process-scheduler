#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#define MAX_PROCESSES 100
#define MAX_HISTORY 200
#define MAX_PROGRAM_NAME 256
#define STATE_WAITING 1
#define STATE_RUNNING 0
#define STATE_FINISHED -1

typedef struct {
    pid_t pid;
    char command[MAX_PROGRAM_NAME];
    int priority;
    int state;
    time_t submission_time;
    time_t start_time;
    time_t completion_time;
    int wait_time;
} Process;

typedef struct {
    Process processes[MAX_PROCESSES];
    int rear;
} ProcessQueue;

typedef struct {
    Process processes[MAX_HISTORY];
    int count;
} History;

int NCPU = 1;
int TSLICE = 1000;
volatile sig_atomic_t terminate_flag = 0;

ProcessQueue *ready_queue = NULL;
History *shared_history = NULL;
int shmid_queue = -1, shmid_history = -1, shmid_sem = -1;
sem_t *scheduler_sem = NULL;
sem_t *queue_sem = NULL;

void handle_sigint(int sig) { (void)sig; terminate_flag = 1; }

void handle_sigchld(int sig) {
    (void)sig;
    pid_t pid; int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {}
}

void setup_signals() {
    struct sigaction sa_int, sa_chld;
    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask); sa_int.sa_flags = 0;
    if (sigaction(SIGINT, &sa_int, NULL) == -1) { perror("sigaction SIGINT"); exit(EXIT_FAILURE); }

    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask); sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) { perror("sigaction SIGCHLD"); exit(EXIT_FAILURE); }
}

void init_ipc() {
    shmid_queue = shmget(IPC_PRIVATE, sizeof(ProcessQueue), IPC_CREAT | 0666);
    if (shmid_queue < 0) { perror("shmget queue"); exit(EXIT_FAILURE); }
    ready_queue = shmat(shmid_queue, NULL, 0);
    if (ready_queue == (void *)-1) { perror("shmat queue"); exit(EXIT_FAILURE); }
    ready_queue->rear = -1;

    shmid_history = shmget(IPC_PRIVATE, sizeof(History), IPC_CREAT | 0666);
    if (shmid_history < 0) { perror("shmget history"); exit(EXIT_FAILURE); }
    shared_history = shmat(shmid_history, NULL, 0);
    if (shared_history == (void *)-1) { perror("shmat history"); exit(EXIT_FAILURE); }
    shared_history->count = 0;

    shmid_sem = shmget(IPC_PRIVATE, sizeof(sem_t) * 2, IPC_CREAT | 0666);
    if (shmid_sem < 0) { perror("shmget semaphores"); exit(EXIT_FAILURE); }
    sem_t *sems = shmat(shmid_sem, NULL, 0);
    if (sems == (void *)-1) { perror("shmat semaphores"); exit(EXIT_FAILURE); }
    scheduler_sem = &sems[0]; queue_sem = &sems[1];
    if (sem_init(scheduler_sem, 1, 0) == -1) { perror("sem_init scheduler_sem"); exit(EXIT_FAILURE); }
    if (sem_init(queue_sem, 1, 1) == -1) { perror("sem_init queue_sem"); exit(EXIT_FAILURE); }
}

void scheduler_daemon() {
    while (!terminate_flag) {
        if (sem_wait(scheduler_sem) == -1) {
            if (errno == EINTR) continue;
            perror("sem_wait scheduler_sem"); break;
        }
        if (sem_wait(queue_sem) == -1) { perror("sem_wait queue_sem"); continue; }

        int scheduled = 0;
        for (int i = 0; i <= ready_queue->rear && scheduled < NCPU; i++) {
            if (ready_queue->processes[i].state == STATE_WAITING) {
                if (kill(ready_queue->processes[i].pid, SIGCONT) == -1) { perror("kill SIGCONT"); continue; }
                if (ready_queue->processes[i].start_time == 0) {
                    ready_queue->processes[i].start_time = time(NULL);
                    for (int j = 0; j < shared_history->count; j++)
                        if (shared_history->processes[j].pid == ready_queue->processes[i].pid) {
                            shared_history->processes[j].start_time = ready_queue->processes[i].start_time; break;
                        }
                }
                ready_queue->processes[i].state = STATE_RUNNING; scheduled++;
            }
        }
        if (sem_post(queue_sem) == -1) { perror("sem_post queue_sem"); continue; }
        if (scheduled > 0) usleep(TSLICE * 1000);

        if (sem_wait(queue_sem) == -1) { perror("sem_wait queue_sem"); continue; }
        for (int i = 0; i <= ready_queue->rear; i++) {
            if (ready_queue->processes[i].state == STATE_RUNNING) {
                if (kill(ready_queue->processes[i].pid, 0) == 0) {
                    if (kill(ready_queue->processes[i].pid, SIGSTOP) == -1) { perror("kill SIGSTOP"); continue; }
                    ready_queue->processes[i].state = STATE_WAITING;
                } else {
                    ready_queue->processes[i].state = STATE_FINISHED;
                    ready_queue->processes[i].completion_time = time(NULL);
                    for (int j = 0; j < shared_history->count; j++)
                        if (shared_history->processes[j].pid == ready_queue->processes[i].pid) {
                            shared_history->processes[j].completion_time = ready_queue->processes[i].completion_time;
                            shared_history->processes[j].start_time = ready_queue->processes[i].start_time; break;
                        }
                }
            }
        }
        if (sem_post(queue_sem) == -1) perror("sem_post queue_sem");
    }
}

void add_to_history_and_queue(pid_t pid, const char *prog, int priority) {
    time_t now = time(NULL);
    if (shared_history->count < MAX_HISTORY) {
        Process *p = &shared_history->processes[shared_history->count++];
        strncpy(p->command, prog, MAX_PROGRAM_NAME - 1); p->command[MAX_PROGRAM_NAME - 1] = '\0';
        p->pid = pid; p->priority = priority; p->submission_time = now;
        p->start_time = 0; p->completion_time = 0; p->wait_time = 0; p->state = STATE_WAITING;
    }

    if (sem_wait(queue_sem) == -1) { perror("sem_wait queue_sem"); return; }
    if (ready_queue->rear < MAX_PROCESSES - 1) {
        Process proc;
        proc.pid = pid; strncpy(proc.command, prog, MAX_PROGRAM_NAME - 1); proc.command[MAX_PROGRAM_NAME - 1] = '\0';
        proc.priority = priority; proc.state = STATE_WAITING; proc.submission_time = now;
        proc.start_time = 0; proc.completion_time = 0; proc.wait_time = 0;
        int i = ready_queue->rear;
        while (i >= 0 && ready_queue->processes[i].priority < priority) { ready_queue->processes[i + 1] = ready_queue->processes[i]; i--; }
        ready_queue->processes[i + 1] = proc; ready_queue->rear++;
        printf("Job submitted: PID=%d, Executable=%s, Priority=%d\n", pid, prog, priority);
        if (sem_post(scheduler_sem) == -1) perror("sem_post scheduler_sem");
    } else {
        printf("Error: Ready queue is full. Cannot submit job.\n"); kill(pid, SIGKILL);
    }
    if (sem_post(queue_sem) == -1) perror("sem_post queue_sem");
}

void cleanup() {
    printf("\nJob History:\n");
    for (int i = 0; i < shared_history->count; i++) {
        Process *p = &shared_history->processes[i];
        printf("Job [%d]:\n", i + 1);
        printf("    Command: %s\n", p->command);
        printf("    PID: %d\n", p->pid);
        printf("    Priority: %d\n", p->priority);
        printf("    Submission: %s", ctime(&p->submission_time));
        if (p->start_time) printf("    Start: %s", ctime(&p->start_time));
        if (p->completion_time) printf("    Completion: %s", ctime(&p->completion_time));
        printf("\n");
    }
    shmdt(ready_queue); shmctl(shmid_queue, IPC_RMID, NULL);
    shmdt(shared_history); shmctl(shmid_history, IPC_RMID, NULL);
    sem_destroy(scheduler_sem); sem_destroy(queue_sem); shmdt(scheduler_sem); shmctl(shmid_sem, IPC_RMID, NULL);
}
