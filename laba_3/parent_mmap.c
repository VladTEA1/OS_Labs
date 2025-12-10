#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <semaphore.h>

#define SHM_SIZE 65536  // 64KB shared memory
#define SHM_NAME "/lab3_shm"
#define SEM_PARENT_NAME "/lab3_sem_parent"
#define SEM_CHILD1_NAME "/lab3_sem_child1"
#define SEM_CHILD2_NAME "/lab3_sem_child2"

typedef struct {
    char data[SHM_SIZE - 20];
    int line_number;
    int length;
    int is_end;
} SharedData;

int main(void) {
    char *fname1 = NULL, *fname2 = NULL;
    size_t fncap = 0;
    ssize_t r;

    printf("Enter filename for child1 (нечетные строки): ");
    fflush(stdout);
    r = getline(&fname1, &fncap, stdin);
    if (r <= 0) { 
        perror("getline fname1"); 
        free(fname1); 
        return 1; 
    }
    if (fname1[r-1] == '\n') fname1[r-1] = '\0';

    printf("Enter filename for child2 (четные строки): ");
    fflush(stdout);
    fncap = 0;
    r = getline(&fname2, &fncap, stdin);
    if (r <= 0) { 
        perror("getline fname2"); 
        free(fname1); 
        free(fname2); 
        return 1; 
    }
    if (fname2[r-1] == '\n') fname2[r-1] = '\0';

    // ====== СОЗДАЕМ РАЗДЕЛЯЕМУЮ ПАМЯТЬ (mmap) ======
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        free(fname1); free(fname2);
        return 1;
    }
    
    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        perror("ftruncate");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        free(fname1); free(fname2);
        return 1;
    }
    
    SharedData *shared = mmap(NULL, SHM_SIZE, 
                              PROT_READ | PROT_WRITE, 
                              MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        free(fname1); free(fname2);
        return 1;
    }
    
    close(shm_fd);
    
    // Инициализация разделяемой памяти
    memset(shared, 0, sizeof(SharedData));
    shared->line_number = 0;
    shared->length = 0;
    shared->is_end = 0;

    // ====== СОЗДАЕМ СЕМАФОРЫ ДЛЯ СИНХРОНИЗАЦИИ ======
    sem_unlink(SEM_PARENT_NAME);
    sem_unlink(SEM_CHILD1_NAME);
    sem_unlink(SEM_CHILD2_NAME);
    
    sem_t *sem_parent = sem_open(SEM_PARENT_NAME, O_CREAT, 0666, 0);
    sem_t *sem_child1 = sem_open(SEM_CHILD1_NAME, O_CREAT, 0666, 0);
    sem_t *sem_child2 = sem_open(SEM_CHILD2_NAME, O_CREAT, 0666, 0);
    
    if (sem_parent == SEM_FAILED || 
        sem_child1 == SEM_FAILED || 
        sem_child2 == SEM_FAILED) {
        perror("sem_open");
        munmap(shared, SHM_SIZE);
        shm_unlink(SHM_NAME);
        free(fname1); free(fname2);
        return 1;
    }

    // ====== СОЗДАЕМ ДОЧЕРНИЙ ПРОЦЕСС 1 ======
    pid_t pid1 = fork();
    if (pid1 < 0) {
        perror("fork pid1");
        sem_close(sem_parent);
        sem_close(sem_child1);
        sem_close(sem_child2);
        sem_unlink(SEM_PARENT_NAME);
        sem_unlink(SEM_CHILD1_NAME);
        sem_unlink(SEM_CHILD2_NAME);
        munmap(shared, SHM_SIZE);
        shm_unlink(SHM_NAME);
        free(fname1); free(fname2);
        return 1;
    }

    if (pid1 == 0) {
        // ДОЧЕРНИЙ ПРОЦЕСС 1 (нечетные строки)
        execl("./child_mmap", "child_mmap", "1", fname1, 
              SHM_NAME, SEM_PARENT_NAME, SEM_CHILD1_NAME, (char *)NULL);
        perror("execl child_mmap");
        _exit(1);
    }

    // ====== СОЗДАЕМ ДОЧЕРНИЙ ПРОЦЕСС 2 ======
    pid_t pid2 = fork();
    if (pid2 < 0) {
        perror("fork pid2");
        kill(pid1, SIGTERM);
        sem_close(sem_parent);
        sem_close(sem_child1);
        sem_close(sem_child2);
        sem_unlink(SEM_PARENT_NAME);
        sem_unlink(SEM_CHILD1_NAME);
        sem_unlink(SEM_CHILD2_NAME);
        munmap(shared, SHM_SIZE);
        shm_unlink(SHM_NAME);
        free(fname1); free(fname2);
        return 1;
    }

    if (pid2 == 0) {
        // ДОЧЕРНИЙ ПРОЦЕСС 2 (четные строки)
        execl("./child_mmap", "child_mmap", "2", fname2, 
              SHM_NAME, SEM_PARENT_NAME, SEM_CHILD2_NAME, (char *)NULL);
        perror("execl child_mmap");
        _exit(1);
    }

    // ====== РОДИТЕЛЬСКИЙ ПРОЦЕСС: ЧТЕНИЕ СТРОК ======
    printf("Введите строки (Ctrl+D для завершения):\n");
    fflush(stdout);
    
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    long lineno = 0;
    
    while ((linelen = getline(&line, &linecap, stdin)) != -1) {
        lineno++;
        
        // Определяем, какому процессу отправить строку
        int child_id;
        sem_t *child_sem;
        
        if (lineno % 2 == 1) {  // НЕЧЕТНАЯ строка -> child1
            child_id = 1;
            child_sem = sem_child1;
            printf("[Родитель] Строка %ld -> child1 (нечетные)\n", lineno);
        } else {  // ЧЕТНАЯ строка -> child2
            child_id = 2;
            child_sem = sem_child2;
            printf("[Родитель] Строка %ld -> child2 (четные)\n", lineno);
        }
        
        // Копируем данные в разделяемую память
        strncpy(shared->data, line, linelen);
        shared->data[linelen] = '\0';
        shared->line_number = lineno;
        shared->length = linelen;
        shared->is_end = 0;
        
        // Сигнализируем дочернему процессу
        sem_post(child_sem);
        
        // Ждем, пока дочерний процесс прочитает данные
        sem_wait(sem_parent);
    }

    free(line);
    
    // ====== ОТПРАВКА СИГНАЛА ЗАВЕРШЕНИЯ ======
    shared->is_end = 1;
    
    // Сигнализируем обоим дочерним процессам
    sem_post(sem_child1);
    sem_post(sem_child2);
    
    // Ждем подтверждения от обоих процессов
    sem_wait(sem_parent);
    sem_wait(sem_parent);

    // ====== ОЖИДАНИЕ ЗАВЕРШЕНИЯ ДОЧЕРНИХ ПРОЦЕССОВ ======
    printf("\n[Родитель] Ожидание завершения дочерних процессов...\n");
    
    int status;
    waitpid(pid1, &status, 0);
    printf("[Родитель] child1 завершился с кодом %d\n", WEXITSTATUS(status));
    
    waitpid(pid2, &status, 0);
    printf("[Родитель] child2 завершился с кодом %d\n", WEXITSTATUS(status));

    // ====== ОЧИСТКА РЕСУРСОВ ======
    sem_close(sem_parent);
    sem_close(sem_child1);
    sem_close(sem_child2);
    
    sem_unlink(SEM_PARENT_NAME);
    sem_unlink(SEM_CHILD1_NAME);
    sem_unlink(SEM_CHILD2_NAME);
    
    munmap(shared, SHM_SIZE);
    shm_unlink(SHM_NAME);
    
    free(fname1);
    free(fname2);
    
    printf("[Родитель] Работа завершена.\n");
    return 0;
}