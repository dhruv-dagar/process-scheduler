#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <time.h>

#define MAX_PROCESSES 100
#define MAX_HISTORY 200
#define MAX_PROGRAM_NAME 256
#define STATE_WAITING 1
#define STATE_RUNNING 0
#define STATE_FINISHED -1
#define MAX_PRIORITY 4

typedef struct { pid_t pid; char command[MAX_PROGRAM_NAME]; int priority; int state; time_t submission_time; time_t start_time; time_t completion_time; int wait_time; } Process;
typedef struct { Process processes[MAX_PROCESSES]; int rear; } ProcessQueue;
typedef struct { Process processes[MAX_HISTORY]; int count; } History;

extern int NCPU; extern int TSLICE; extern volatile sig_atomic_t terminate_flag;
extern ProcessQueue *ready_queue; extern History *shared_history;
extern sem_t *scheduler_sem; extern sem_t *queue_sem;

void handle_sigint(int sig); void handle_sigchld(int sig); void setup_signals(); void init_ipc(); void scheduler_daemon();
void add_to_history_and_queue(pid_t pid, const char *prog, int priority); void cleanup();

static void trim_whitespace(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
}

int main(int argc, char *argv[]) {
    if (argc != 3) { fprintf(stderr, "Usage: %s <NCPU> <TSLICE(ms)>\n", argv[0]); return EXIT_FAILURE; }
    NCPU = atoi(argv[1]); TSLICE = atoi(argv[2]);
    if (NCPU <= 0 || TSLICE <= 0) { fprintf(stderr, "Invalid NCPU or TSLICE values\n"); return EXIT_FAILURE; }
    setup_signals(); init_ipc();

    pid_t scheduler_pid = fork();
    if (scheduler_pid < 0) { perror("fork scheduler"); return EXIT_FAILURE; }
    if (scheduler_pid == 0) { scheduler_daemon(); exit(0); }

    char input[MAX_PROGRAM_NAME];
    while (!terminate_flag) {
        printf("SimpleScheduler$ :~$ "); fflush(stdout);
        if (fgets(input, sizeof(input), stdin) == NULL) { if (errno == EINTR) continue; break; }
        input[strcspn(input, "\n")] = 0; trim_whitespace(input);
        if (!strlen(input)) continue;
        char *cmd = strtok(input, " "); if (!cmd) continue;

        if (!strcmp(cmd, "exit")) { terminate_flag = 1; break; }
        if (!strcmp(cmd, "submit")) {
            char *prog = strtok(NULL, " "); char *prio_str = strtok(NULL, " ");
            if (!prog) { printf("Error: No executable specified.\n"); continue; }
            int priority = 1;
            if (prio_str) { priority = atoi(prio_str); if (priority < 1 || priority > MAX_PRIORITY) { printf("Invalid priority. Using default priority 1.\n"); priority = 1; } }
            pid_t child_pid = fork();
            if (child_pid < 0) { perror("fork"); continue; }
            if (child_pid == 0) {
                raise(SIGSTOP);
                execl(prog, prog, (char *)NULL);
                perror("execl failed"); exit(EXIT_FAILURE);
            }
            add_to_history_and_queue(child_pid, prog, priority);
        } else printf("Error: Unknown command. Use 'submit' or 'exit'.\n");
    }

    kill(scheduler_pid, SIGTERM); waitpid(scheduler_pid, NULL, 0); cleanup();
    printf("Shell terminated. Exiting.\n"); return 0;
}
