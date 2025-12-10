#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

int global_min = INT_MAX;
int global_max = INT_MIN;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t start_semaphore;

typedef struct {
    int *array;
    int start;
    int end;
    int id;
} ThreadData;

void* find_min_max(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    
    sem_wait(&start_semaphore);
    
    int local_min = INT_MAX;
    int local_max = INT_MIN;

    for (int i = data->start; i < data->end; i++) {
        volatile double dummy = 0;
        for (int j = 0; j < 50; j++) {
            dummy += (data->array[i] * j) / 1000.0;
        }
        
        if (data->array[i] < local_min) local_min = data->array[i];
        if (data->array[i] > local_max) local_max = data->array[i];
    }

    pthread_mutex_lock(&mutex);
    if (local_min < global_min) global_min = local_min;
    if (local_max > global_max) global_max = local_max;
    printf("Thread %d: min=%d, max=%d\n", data->id, local_min, local_max);
    pthread_mutex_unlock(&mutex);

    pthread_exit(NULL);
}

int* read_array_from_file(const char* filename, int* size) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("File error");
        return NULL;
    }

    int count = 0;
    int temp;
    while (fscanf(file, "%d", &temp) == 1) count++;
    
    if (count == 0) {
        fclose(file);
        return NULL;
    }
    
    rewind(file);
    int* array = (int*)malloc(count * sizeof(int));
    if (!array) {
        fclose(file);
        return NULL;
    }
    
    for (int i = 0; i < count; i++) {
        fscanf(file, "%d", &array[i]);
    }
    
    fclose(file);
    *size = count;
    return array;
}

void generate_test_file(const char* filename, int count) {
    FILE *file = fopen(filename, "w");
    if (!file) return;
    
    srand(time(NULL));
    for (int i = 0; i < count; i++) {
        fprintf(file, "%d\n", rand() % 1000000);
    }
    
    fclose(file);
    printf("File %s created with %d numbers\n", filename, count);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <filename> <threads>\n", argv[0]);
        printf("Or: %s --generate <count>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--generate") == 0) {
        int count = atoi(argv[2]);
        char filename[100];
        sprintf(filename, "numbers_%d.txt", count);
        generate_test_file(filename, count);
        return 0;
    }

    char* filename = argv[1];
    int num_threads = atoi(argv[2]);
    if (num_threads <= 0) return 1;

    sem_init(&start_semaphore, 0, 0);

    int size;
    int* array = read_array_from_file(filename, &size);
    if (!array) return 1;

    if (num_threads > size) num_threads = size;

    printf("PID: %d, Array size: %d, Threads: %d\n", getpid(), size, num_threads);
    printf("Initial threads:\n");
    char cmd[100];
    sprintf(cmd, "ps -L -p %d -o pid,tid | grep -v PID", getpid());
    system(cmd);

    struct timeval start, end;
    gettimeofday(&start, NULL);

    pthread_t threads[num_threads];
    ThreadData thread_data[num_threads];
    int chunk = size / num_threads;

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].array = array;
        thread_data[i].start = i * chunk;
        thread_data[i].end = (i == num_threads - 1) ? size : (i + 1) * chunk;
        thread_data[i].id = i + 1;
        pthread_create(&threads[i], NULL, find_min_max, &thread_data[i]);
    }

    printf("\nThreads created (waiting). Check in another terminal:\n");
    printf("ps -eLf | grep %d | grep -v grep\n", getpid());
    sleep(5);

    for (int i = 0; i < num_threads; i++) {
        sem_post(&start_semaphore);
    }

    printf("\nThreads started. Check CPU usage:\n");
    printf("top -H -p %d\n", getpid());
    sleep(5);

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    gettimeofday(&end, NULL);
    double time = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;

    printf("\nResults: min=%d, max=%d\n", global_min, global_max);
    printf("Time: %.3f sec, Speed: %.0f nums/sec\n", time, size / time);

    printf("\nFinal threads:\n");
    system(cmd);

    pthread_mutex_destroy(&mutex);
    sem_destroy(&start_semaphore);
    free(array);

    return 0;
}


