# Process Scheduler

A Linux process-scheduling simulator in C that demonstrates OS-level scheduling concepts using real processes, signals, shared memory, and process-shared POSIX semaphores.

## Highlights

- Configurable CPU count and time slice
- Priority-based ready queue
- Preemptive execution using `SIGCONT` and `SIGSTOP`
- Separate scheduler daemon and interactive submission shell
- Shared-memory process queue and execution history
- Process-shared semaphore synchronization
- Signal-driven process lifecycle handling
- Included CPU-bound workloads for testing

## Build

```bash
make
```

## Run

Start the scheduler shell with the number of simulated CPUs and time slice in milliseconds:

```bash
./simplescheduler 2 200
```

Submit jobs from the interactive prompt:

```text
SimpleScheduler$ :~$ submit ./dummy_a 3
SimpleScheduler$ :~$ submit ./dummy_b 1
SimpleScheduler$ :~$ exit
```

## Architecture

```text
             +----------------------+
             |   Scheduler Shell    |
             |  submit / exit       |
             +----------+-----------+
                        |
                     fork()
                        |
            +-----------+-----------+
            |                       |
            v                       v
     Child workload            Scheduler daemon
     SIGSTOP -> exec           SIGCONT / SIGSTOP
                                    |
                         +----------+----------+
                         | Shared Memory       |
                         | Ready Queue/History |
                         +----------+----------+
                                    |
                         Process-shared semaphores
```

The scheduler releases up to `NCPU` waiting processes for each time slice, then preempts still-running processes and returns them to the ready queue.

## OS concepts demonstrated

- CPU scheduling and time slicing
- Priority scheduling
- Process creation with `fork`
- Process control with POSIX signals
- Inter-process communication with System V shared memory
- Synchronization with process-shared semaphores
- Process lifecycle and reaping with `waitpid`

## Portfolio value

This project is useful for demonstrating practical understanding of process management, scheduling, IPC, synchronization, and Linux systems programming.

## Author

Dhruv Dagar
