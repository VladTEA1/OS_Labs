#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>

#define SHM_SIZE 65536

typedef struct {
    char data[SHM_SIZE - 20];
    int line_number;
    int length;
    int is_end;
} SharedData;

// Функция для инвертирования строки
void reverse_string(char *str, size_t len) {
    for (size_t i = 0, j = len - 1; i < j; ++i, --j) {
        char tmp = str[i];
        str[i] = str[j];
        str[j] = tmp;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <child_id> <output_file> <shm_name> <sem_parent> <sem_child>\n", argv[0]);
        fprintf(stderr, "  child_id: 1 - нечетные строки, 2 - четные строки\n");
        return 1;
    }

    int child_id = atoi(argv[1]);
    const char *outname = argv[2];
    const char *shm_name = argv[3];
    const char *sem_parent_name = argv[4];
    const char *sem_child_name = argv[5];
    
    // Определяем тип процесса
    const char *process_type;
    const char *file_header;
    
    if (child_id == 1) {
        process_type = "child1 (нечетные строки)";
        file_header = "=== Child1 (нечетные строки через mmap) ===\n";
    } else if (child_id == 2) {
        process_type = "child2 (четные строки)";
        file_header = "=== Child2 (четные строки через mmap) ===\n";
    } else {
        fprintf(stderr, "Ошибка: child_id должен быть 1 или 2\n");
        return 1;
    }

    // ====== ОТКРЫВАЕМ ФАЙЛ ДЛЯ ЗАПИСИ ======
    FILE *out = fopen(outname, "w");
    if (!out) {
        perror("fopen output file");
        return 1;
    }
    
    fprintf(out, "%s", file_header);
    fprintf(out, "Запущен: %s", ctime(&(time_t){time(NULL)}));
    
    // ====== ОТКРЫВАЕМ РАЗДЕЛЯЕМУЮ ПАМЯТЬ ======
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open child");
        fclose(out);
        return 1;
    }
    
    SharedData *shared = mmap(NULL, SHM_SIZE, 
                              PROT_READ | PROT_WRITE, 
                              MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap child");
        close(shm_fd);
        fclose(out);
        return 1;
    }
    
    close(shm_fd);
    
    // ====== ОТКРЫВАЕМ СЕМАФОРЫ ======
    sem_t *sem_parent = sem_open(sem_parent_name, 0);
    sem_t *sem_child = sem_open(sem_child_name, 0);
    
    if (sem_parent == SEM_FAILED || sem_child == SEM_FAILED) {
        perror("sem_open child");
        munmap(shared, SHM_SIZE);
        fclose(out);
        return 1;
    }
    
    // ====== ИНФОРМАЦИЯ О ЗАПУСКЕ ======
    printf("[%s] Процесс запущен.\n", process_type);
    printf("[%s] Файл вывода: %s\n", process_type, outname);
    printf("[%s] Ожидание данных через mmap...\n", process_type);
    
    int line_count = 0;
    
    // ====== ОСНОВНОЙ ЦИКЛ ОБРАБОТКИ ======
    while (1) {
        // Ждем, пока родитель положит данные
        sem_wait(sem_child);
        
        // Проверяем флаг завершения
        if (shared->is_end) {
            break;
        }
        
        // Пропускаем строки, которые не предназначены этому процессу
        if ((child_id == 1 && shared->line_number % 2 == 0) ||
            (child_id == 2 && shared->line_number % 2 == 1)) {
            // Не наша строка, сигнализируем родителю и продолжаем
            sem_post(sem_parent);
            continue;
        }
        
        line_count++;
        
        // Сохраняем оригинальную строку
        char original[1024];
        int len = shared->length;
        if (len >= sizeof(original)) len = sizeof(original) - 1;
        
        strncpy(original, shared->data, len);
        original[len] = '\0';
        
        // Убираем символ новой строки
        if (original[len-1] == '\n') {
            original[len-1] = '\0';
        }
        
        // Инвертируем строку
        char reversed[1024];
        strncpy(reversed, original, sizeof(reversed));
        reversed[sizeof(reversed)-1] = '\0';
        
        size_t real_len = strlen(reversed);
        reverse_string(reversed, real_len);
        
        // Выводим результат в консоль
        printf("[%s] Строка %d (общая %d): '%s' -> '%s'\n", 
               process_type, line_count, shared->line_number, 
               original, reversed);
        
        // Записываем в файл
        fprintf(out, "Строка %d (общая %d):\n", line_count, shared->line_number);
        fprintf(out, "  Оригинал: %s\n", original);
        fprintf(out, "  Инвертировано: %s\n\n", reversed);
        fflush(out);
        
        // Сигнализируем родителю, что данные обработаны
        sem_post(sem_parent);
    }
    
    // ====== ЗАВЕРШЕНИЕ РАБОТЫ ======
    printf("[%s] Получено %d строк. Завершение работы.\n", 
           process_type, line_count);
    
    fprintf(out, "=== Всего обработано строк: %d ===\n", line_count);
    fprintf(out, "Завершен: %s", ctime(&(time_t){time(NULL)}));
    
    // Последний сигнал родителю
    sem_post(sem_parent);
    
    // ====== ОЧИСТКА РЕСУРСОВ ======
    sem_close(sem_parent);
    sem_close(sem_child);
    munmap(shared, SHM_SIZE);
    fclose(out);
    
    return 0;
}