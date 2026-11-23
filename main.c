#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

#define NUM_CORES 4

// 1. Task Structure (Linked List Node)
typedef struct Task {
    int id;
    int burst_time;
    int remaining_time;
    struct Task* next;
} Task;

// 2. CPU Core Structure
typedef struct {
    int id;
    Task* head;
    Task* tail;
    int active_tasks;
    int total_remaining_load;
    pthread_mutex_t lock;
    pthread_t thread_id;
} CPUCore;

// Global Variables
CPUCore cores[NUM_CORES];
volatile bool running = true;
int global_task_counter = 1;
pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;

// 3. Algorithm: Least Loaded Assignment
void assign_least_loaded(Task* task) {
    int best_core = 0;
    int min_load = 1e9;

    // Sabse kam busy core dhundna
    for (int i = 0; i < NUM_CORES; i++) {
        pthread_mutex_lock(&cores[i].lock);
        if (cores[i].total_remaining_load < min_load) {
            min_load = cores[i].total_remaining_load;
            best_core = i;
        }
        pthread_mutex_unlock(&cores[i].lock);
    }

    // Task ko selected core ki queue mein add karna
    pthread_mutex_lock(&cores[best_core].lock);
    if (cores[best_core].tail == NULL) {
        cores[best_core].head = cores[best_core].tail = task;
    } else {
        cores[best_core].tail->next = task;
        cores[best_core].tail = task;
    }
    cores[best_core].active_tasks++;
    cores[best_core].total_remaining_load += task->burst_time;
    pthread_mutex_unlock(&cores[best_core].lock);
}

// 4. Worker Thread (Core process simulation)
void* cpu_worker(void* arg) {
    CPUCore* core = (CPUCore*)arg;

    while (running) {
        pthread_mutex_lock(&core->lock);
        Task* current = core->head;

        if (current != NULL) {
            current->remaining_time--;
            core->total_remaining_load--;

            // Task complete hone par free karna
            if (current->remaining_time <= 0) {
                core->head = current->next;
                if (core->head == NULL) {
                    core->tail = NULL;
                }
                core->active_tasks--;
                free(current);
            }
        }
        pthread_mutex_unlock(&core->lock);
        sleep(1);
    }
    return NULL;
}

// 5. Generator Thread (Naye tasks randomly create karta hai)
void* task_generator(void* arg) {
    while (running && global_task_counter <= 25) {
        Task* new_task = (Task*)malloc(sizeof(Task));

        pthread_mutex_lock(&counter_lock);
        new_task->id = global_task_counter++;
        pthread_mutex_unlock(&counter_lock);

        new_task->burst_time = (rand() % 5) + 2; // 2 se 6 seconds ka time
        new_task->remaining_time = new_task->burst_time;
        new_task->next = NULL;

        assign_least_loaded(new_task);

        usleep((rand() % 1000 + 500) * 1000); // Dynamic delay
    }
    return NULL;
}

// 6. UI Dashboard Thread (ANSI colors ke sath screen update)
void* render_dashboard(void* arg) {
    while (running) {
        printf("\033[H\033[J"); // Screen clear command
        printf("===================================================\n");
        printf("       CPU LOAD BALANCING SIMULATOR (PURE C)      \n");
        printf("===================================================\n\n");

        for (int i = 0; i < NUM_CORES; i++) {
            pthread_mutex_lock(&cores[i].lock);
            int active = cores[i].active_tasks;
            int load = cores[i].total_remaining_load;

            printf("[Core %d] Tasks Queue: %2d | Total Load: %2ds\n", cores[i].id, active, load);

            // Progress Bar
            printf("Load Bar: [");
            int bar_length = load > 20 ? 20 : load;
            for (int b = 0; b < 20; b++) {
                if (b < bar_length) printf("#");
                else printf(" ");
            }
            printf("]\n");

            if (cores[i].head != NULL) {
                printf("Executing -> Task %d (Time Left: %ds / Total: %ds)\n",
                       cores[i].head->id, cores[i].head->remaining_time, cores[i].head->burst_time);
            } else {
                printf("Executing -> IDLE\n");
            }
            pthread_mutex_unlock(&cores[i].lock);
            printf("---------------------------------------------------\n");
        }

        fflush(stdout);
        usleep(400000); // 0.4 sec dashboard refresh speed
    }
    return NULL;
}

// 7. Main Function
int main() {
    srand(time(NULL));

    // Cores Initialize karna
    for (int i = 0; i < NUM_CORES; i++) {
        cores[i].id = i;
        cores[i].head = cores[i].tail = NULL;
        cores[i].active_tasks = 0;
        cores[i].total_remaining_load = 0;
        pthread_mutex_init(&cores[i].lock, NULL);
        pthread_create(&cores[i].thread_id, NULL, cpu_worker, &cores[i]);
    }

    pthread_t gen_thread, ui_thread;
    pthread_create(&gen_thread, NULL, task_generator, NULL);
    pthread_create(&ui_thread, NULL, render_dashboard, NULL);

    pthread_join(gen_thread, NULL);
    sleep(6); // Remaining tasks execute hone ka wait
    running = false;

    pthread_join(ui_thread, NULL);
    for (int i = 0; i < NUM_CORES; i++) {
        pthread_join(cores[i].thread_id, NULL);
        pthread_mutex_destroy(&cores[i].lock);
    }

    printf("\nSimulation Completed Successfully!\n");
    return 0;
}