#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>

/* ============================================================
   GLOBAL VARIABLES FOR CO-6 (CONCURRENCY & SYNCHRONIZATION)
   ============================================================ */
#define THREAD_COUNT 4
#define ITERATIONS 100000

long long shared_counter = 0;
int use_mutex = 0;

pthread_mutex_t counter_mutex;
pthread_mutex_t cond_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t data_cond = PTHREAD_COND_INITIALIZER;
int data_ready = 0;

sem_t counting_sem;

pthread_mutex_t lockA = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lockB = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================
   CO-1 : SYSTEM CALLS AND COMMAND EXECUTION
   ============================================================ */
void co1_system_calls() {
    printf("\n============================================\n");
    printf("CO-1 : SYSTEM CALLS AND COMMAND EXECUTION\n");
    printf("============================================\n");

    printf("Parent Process ID (getpid): %d\n", getpid());
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        return;
    } else if (pid == 0) {
        printf("[Child] PID: %d | Executing 'ls -l' via execlp()...\n\n", getpid());
        execlp("ls", "ls", "-l", NULL);
        perror("execlp failed");
        exit(1);
    } else {
        wait(NULL);
        printf("\n[Parent] Child command execution completed.\n");
    }
}

/* ============================================================
   CO-2 : PROCESS CREATION AND LIFECYCLE
   ============================================================ */
void co2_process() {
    printf("\n============================================\n");
    printf("CO-2 : PROCESS CREATION AND LIFECYCLE\n");
    printf("============================================\n");

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        return;
    } else if (pid == 0) {
        printf("[Child] State: CREATED -> RUNNING\n");
        printf("[Child] PID: %d | Parent PID: %d\n", getpid(), getppid());
        printf("[Child] Sleeping for 1 second (WAITING/SLEEPING state)...\n");
        sleep(1);
        printf("[Child] State: TERMINATING (exit 0)\n");
        exit(0);
    } else {
        printf("[Parent] Waiting for child PID %d to finish...\n", pid);
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            printf("[Parent] Child reaped cleanly with exit status: %d\n", WEXITSTATUS(status));
        }
    }
}

/* ============================================================
   CO-3 : INTER-PROCESS COMMUNICATION (ANONYMOUS PIPE)
   ============================================================ */
void co3_pipe() {
    int fd[2];
    char message[] = "IPC Pipe Message from Parent Process";
    char buffer[128];

    printf("\n============================================\n");
    printf("CO-3 : IPC USING ANONYMOUS PIPE\n");
    printf("============================================\n");

    if (pipe(fd) == -1) {
        perror("pipe failed");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return;
    } else if (pid == 0) {
        close(fd[1]); // Close write end
        ssize_t n = read(fd[0], buffer, sizeof(buffer) - 1);
        if (n >= 0) buffer[n] = '\0';
        printf("[Child] Read from pipe: \"%s\"\n", buffer);
        close(fd[0]);
        exit(0);
    } else {
        close(fd[0]); // Close read end
        printf("[Parent] Writing to pipe: \"%s\"\n", message);
        write(fd[1], message, strlen(message) + 1);
        close(fd[1]);
        wait(NULL);
    }
}

/* ============================================================
   CO-4 : MEMORY MANAGEMENT
   Topics:
   - Virtual Memory & Page Size (sysconf)
   - Linux Process Address Space Layout (Text, Data, BSS, Heap, Stack)
   - Dynamic Memory Allocation (malloc, free)
   - Copy-on-Write (CoW) behavior across fork()
   - Linux /proc/<pid>/maps & Page Fault inspection
   ============================================================ */
int global_initialized_var = 42;
int global_uninitialized_bss;

void co4_memory() {
    int stack_local_var = 10;

    printf("\n============================================\n");
    printf("CO-4 : MEMORY MANAGEMENT & VIRTUAL MEMORY\n");
    printf("============================================\n");

    /* 1. Virtual Memory & Page Tables Info */
    long page_size = sysconf(_SC_PAGESIZE);
    printf("\n--- 1. VIRTUAL MEMORY & PAGE TABLES ---\n");
    printf("Process PID            : %d\n", getpid());
    printf("System Page Size       : %ld bytes (%ld KB)\n", page_size, page_size / 1024);

    /* 2. Linux Process Address Space Layout */
    int *heap_var = (int *)malloc(sizeof(int));
    *heap_var = 99;

    printf("\n--- 2. LINUX PROCESS ADDRESS SPACE LAYOUT ---\n");
    printf("Text Segment (Function) : %p\n", (void *)co4_memory);
    printf("Data Segment (Init Glob): %p\n", (void *)&global_initialized_var);
    printf("BSS Segment (Uninit)    : %p\n", (void *)&global_uninitialized_bss);
    printf("Heap Segment (malloc)   : %p\n", (void *)heap_var);
    printf("Stack Segment (Local)   : %p\n", (void *)&stack_local_var);
    free(heap_var);

    /* 3. Dynamic Memory Allocation & Demand Paging */
    printf("\n--- 3. DYNAMIC MEMORY ALLOCATION (HEAP) ---\n");
    int n = 5;
    printf("Enter number of integers to allocate dynamically: ");
    if (scanf("%d", &n) != 1 || n <= 0) n = 5;

    int *arr = (int *)malloc(n * sizeof(int));
    if (!arr) {
        perror("malloc failed");
        return;
    }

    printf("Allocated %zu bytes at heap virtual address %p\n", n * sizeof(int), (void *)arr);
    printf("Populating pages (triggers minor page faults on first touch): ");
    for (int i = 0; i < n; i++) {
        arr[i] = (i + 1) * 10;
        printf("%d ", arr[i]);
    }
    printf("\n");
    free(arr);
    printf("Heap memory freed successfully via free().\n");

    /* 4. Copy-on-Write (CoW) Demonstration */
    printf("\n--- 4. COPY-ON-WRITE (CoW) & PROCESS CREATION ---\n");
    int shared_page_var = 500;
    printf("Before fork(): shared_page_var = %d (Virtual Addr: %p)\n",
           shared_page_var, (void *)&shared_page_var);

    pid_t pid = fork();
    if (pid == 0) {
        printf("[Child] Initially reads same value: %d (Virtual Addr: %p)\n",
               shared_page_var, (void *)&shared_page_var);
        shared_page_var = 999; // Triggers CoW page fault in kernel
        printf("[Child] Modified value to: %d (Virtual Addr still %p, backed by new physical frame!)\n",
               shared_page_var, (void *)&shared_page_var);
        exit(0);
    } else if (pid > 0) {
        wait(NULL);
        printf("[Parent] Value after Child modification remains: %d (Physical frame isolated via CoW)\n",
               shared_page_var);
    }

    /* 5. Live /proc Memory Map & Page Fault Inspection */
    printf("\n--- 5. /proc MEMORY MAP & DEBUGGING INSPECTION ---\n");
    printf("Showing first 8 lines of /proc/%d/maps:\n", getpid());
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "head -n 8 /proc/%d/maps", getpid());
    system(cmd);
    printf("Tip: Use 'valgrind --leak-check=full ./os_project' to debug memory errors.\n");
}

/* ============================================================
   CO-5 : FILE SYSTEMS, FILE ABSTRACTIONS, AND FILE I/O IN LINUX
   Topics:
   - Unix File Abstraction & VFS
   - File Descriptors & System Calls (open, write, lseek, read, close)
   - Buffered (stdio FILE*) vs Unbuffered (syscall fd) I/O
   - Inodes, Directory Entries, and Metadata (stat)
   - Memory-Mapped File I/O (mmap, munmap)
   ============================================================ */
void co5_files() {
    printf("\n============================================\n");
    printf("CO-5 : FILE SYSTEMS AND LINUX FILE I/O\n");
    printf("============================================\n");

    const char *filename = "os_demo.txt";

    /* 1. Unbuffered System Call I/O & File Descriptors */
    printf("\n--- 1. UNBUFFERED SYSTEM CALL I/O & FILE DESCRIPTORS ---\n");
    int fd = open(filename, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open failed");
        return;
    }
    printf("Opened '%s' via VFS | Assigned File Descriptor (FD): %d\n", filename, fd);

    const char *msg = "Line 1: Unbuffered write() system call directly to kernel page cache.\n";
    ssize_t bytes_written = write(fd, msg, strlen(msg));
    printf("Wrote %zd bytes using write().\n", bytes_written);

    lseek(fd, 0, SEEK_SET);
    char buf[128];
    ssize_t bytes_read = read(fd, buf, sizeof(buf) - 1);
    if (bytes_read >= 0) {
        buf[bytes_read] = '\0';
        printf("Read back via lseek() + read(): %s", buf);
    }

    /* 2. Buffered I/O (Standard C Library) */
    printf("\n--- 2. BUFFERED I/O (stdio FILE Stream) ---\n");
    FILE *fp = fopen("os_buffered.txt", "w+");
    if (fp) {
        fprintf(fp, "Buffered I/O accumulates data in user-space buffer before flushing.\n");
        fflush(fp);
        printf("Wrote and flushed 'os_buffered.txt' using fopen()/fprintf()/fflush().\n");
        fclose(fp);
    }

    /* 3. Inodes, Directory Entries, and File Metadata (stat) */
    printf("\n--- 3. INODE & EXT4/VFS METADATA (stat) ---\n");
    struct stat st;
    if (stat(filename, &st) == 0) {
        printf("File Name (dentry)  : %s\n", filename);
        printf("Inode Number        : %lu\n", (unsigned long)st.st_ino);
        printf("File Size           : %ld bytes\n", (long)st.st_size);
        printf("Block Size (VFS)    : %ld bytes\n", (long)st.st_blksize);
        printf("Hard Links Count    : %lu\n", (unsigned long)st.st_nlink);
        printf("Permissions (Octal) : %o\n", st.st_mode & 0777);
    }

    /* 4. Memory-Mapped File I/O (mmap) */
    printf("\n--- 4. MEMORY-MAPPED FILE I/O (mmap) ---\n");
    size_t map_size = st.st_size;
    char *mapped = (char *)mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        perror("mmap failed");
    } else {
        printf("File mapped into virtual memory at address: %p\n", (void *)mapped);
        printf("Reading directly from memory pointer: %.*s", (int)map_size, mapped);

        // Modify file content directly in memory
        memcpy(mapped, "MMAP_1", 6);
        msync(mapped, map_size, MS_SYNC);
        printf("Modified first 6 bytes in RAM via mmap() -> Synced to disk: %.*s",
               (int)map_size, mapped);

        munmap(mapped, map_size);
    }

    close(fd);
    printf("Closed File Descriptor %d.\n", fd);
}

/* ============================================================
   CO-6 : CONCURRENCY AND SYNCHRONIZATION
   Topics:
   - POSIX Threads (pthread_create, pthread_join)
   - Race Conditions vs Mutual Exclusion (Mutexes) & Timing
   - Condition Variables (Producer-Consumer Signaling)
   - Counting Semaphores (Resource Pool Control)
   - Deadlock Hazard & Lock-Ordering Prevention
   ============================================================ */

/* Part A: Race Condition vs Mutex Worker */
void *counter_worker(void *arg) {
    for (int i = 0; i < ITERATIONS; i++) {
        if (use_mutex) {
            pthread_mutex_lock(&counter_mutex);
            shared_counter++;
            pthread_mutex_unlock(&counter_mutex);
        } else {
            shared_counter++; // Unsynchronized critical section (Race condition)
        }
    }
    return NULL;
}

/* Part B: Condition Variable Workers */
void *producer_thread(void *arg) {
    sleep(1);
    pthread_mutex_lock(&cond_mutex);
    data_ready = 1;
    printf("  [Producer Thread] Data produced! Signaling condition variable...\n");
    pthread_cond_signal(&data_cond);
    pthread_mutex_unlock(&cond_mutex);
    return NULL;
}

void *consumer_thread(void *arg) {
    pthread_mutex_lock(&cond_mutex);
    printf("  [Consumer Thread] Waiting for data_ready condition...\n");
    while (!data_ready) {
        pthread_cond_wait(&data_cond, &cond_mutex);
    }
    printf("  [Consumer Thread] Condition signal received! Processing data.\n");
    pthread_mutex_unlock(&cond_mutex);
    return NULL;
}

/* Part C: Counting Semaphore Worker */
void *semaphore_worker(void *arg) {
    long id = (long)arg;
    sem_wait(&counting_sem);
    printf("  [Thread %ld] Acquired semaphore slot (Entered limited resource pool).\n", id);
    usleep(200000); // 200ms simulated work
    printf("  [Thread %ld] Releasing semaphore slot.\n", id);
    sem_post(&counting_sem);
    return NULL;
}

/* Part D: Deadlock Prevention via Lock Ordering */
void *ordered_lock_worker(void *arg) {
    long id = (long)arg;
    // Both threads acquire lockA then lockB in strict global order to prevent circular wait
    pthread_mutex_lock(&lockA);
    pthread_mutex_lock(&lockB);
    printf("  [Thread %ld] Safely acquired Lock A then Lock B (No Deadlock).\n", id);
    pthread_mutex_unlock(&lockB);
    pthread_mutex_unlock(&lockA);
    return NULL;
}

void co6_concurrency() {
    printf("\n============================================\n");
    printf("CO-6 : CONCURRENCY AND SYNCHRONIZATION\n");
    printf("============================================\n");

    /* 1. Race Condition vs Mutex Benchmark */
    printf("\n--- 1. RACE CONDITION VS MUTEX BENCHMARK ---\n");
    printf("Select Mode:\n1. WITHOUT MUTEX (Demonstrate Race Condition)\n2. WITH MUTEX (Thread-Safe Synchronization)\nEnter choice: ");
    int mode = 2;
    if (scanf("%d", &mode) != 1) mode = 2;
    use_mutex = (mode == 2) ? 1 : 0;

    shared_counter = 0;
    pthread_t threads[THREAD_COUNT];
    struct timespec start, end;

    if (use_mutex) pthread_mutex_init(&counter_mutex, NULL);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_create(&threads[i], NULL, counter_worker, NULL);
    }
    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    if (use_mutex) pthread_mutex_destroy(&counter_mutex);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    long long expected = (long long)THREAD_COUNT * ITERATIONS;

    printf("Expected Counter : %lld\n", expected);
    printf("Actual Counter   : %lld\n", shared_counter);
    printf("Execution Time   : %.6f seconds\n", elapsed);
    if (shared_counter == expected) {
        printf("Status           : CORRECT (Mutual exclusion preserved integrity)\n");
    } else {
        printf("Status           : DATA CORRUPTION (Lost %lld updates due to Race Condition!)\n",
               expected - shared_counter);
    }

    /* 2. Condition Variables (Thread Coordination) */
    printf("\n--- 2. CONDITION VARIABLES (PRODUCER-CONSUMER) ---\n");
    data_ready = 0;
    pthread_t prod, cons;
    pthread_create(&cons, NULL, consumer_thread, NULL);
    pthread_create(&prod, NULL, producer_thread, NULL);
    pthread_join(cons, NULL);
    pthread_join(prod, NULL);

    /* 3. Counting Semaphores */
    printf("\n--- 3. COUNTING SEMAPHORES (MAX 2 CONCURRENT THREADS) ---\n");
    sem_init(&counting_sem, 0, 2); // Permit only 2 threads at a time
    pthread_t sem_threads[4];
    for (long i = 1; i <= 4; i++) {
        pthread_create(&sem_threads[i - 1], NULL, semaphore_worker, (void *)i);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(sem_threads[i], NULL);
    }
    sem_destroy(&counting_sem);

    /* 4. Deadlock Prevention (Strict Lock Ordering) */
    printf("\n--- 4. DEADLOCK PREVENTION (STRICT LOCK ORDERING) ---\n");
    pthread_t dl_threads[2];
    pthread_create(&dl_threads[0], NULL, ordered_lock_worker, (void *)1);
    pthread_create(&dl_threads[1], NULL, ordered_lock_worker, (void *)2);
    pthread_join(dl_threads[0], NULL);
    pthread_join(dl_threads[1], NULL);
}

/* ============================================================
   MAIN MENU
   ============================================================ */
int main() {
    int choice;

    while (1) {
        printf("\n====================================================\n");
        printf("   LINUX OPERATING SYSTEMS PROJECT (CO-1 TO CO-6)   \n");
        printf("====================================================\n");
        printf("1. CO-1 : System Calls & Command Execution (fork, exec)\n");
        printf("2. CO-2 : Process Creation & Lifecycle (states, waitpid)\n");
        printf("3. CO-3 : IPC using Anonymous Pipe (pipe, read, write)\n");
        printf("4. CO-4 : Memory Management (Virtual Mem, CoW, /proc/maps)\n");
        printf("5. CO-5 : File Systems & I/O (Unbuffered, Buffered, Inode, mmap)\n");
        printf("6. CO-6 : Concurrency (Mutex, CondVar, Semaphore, Deadlock)\n");
        printf("7. Run All CO Demonstrations Sequentially\n");
        printf("8. Exit\n");
        printf("----------------------------------------------------\n");
        printf("Enter your choice: ");

        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n'); // Clear invalid input
            continue;
        }

        switch (choice) {
            case 1: co1_system_calls(); break;
            case 2: co2_process(); break;
            case 3: co3_pipe(); break;
            case 4: co4_memory(); break;
            case 5: co5_files(); break;
            case 6: co6_concurrency(); break;
            case 7:
                co1_system_calls();
                co2_process();
                co3_pipe();
                co4_memory();
                co5_files();
                co6_concurrency();
                break;
            case 8:
                printf("Exiting OS Demonstrator.\n");
                return 0;
            default:
                printf("Invalid choice. Select 1-8.\n");
        }
    }
    return 0;
}
