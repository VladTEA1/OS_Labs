#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

// Функция для переворачивания строки
void reverse_string(char *str, size_t len) {
    for (size_t i = 0, j = len - 1; i < j; ++i, --j) {
        char tmp = str[i];
        str[i] = str[j];
        str[j] = tmp;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s output_filename\n", argv[0]);
        return 1;
    }

    const char *outname = argv[1];
    FILE *out = fopen(outname, "w");
    if (!out) {
        perror("fopen output file");
        return 1;
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t n;

    while ((n = getline(&line, &cap, stdin)) != -1) {
        size_t len = (n > 0 && line[n-1] == '\n') ? (size_t)(n - 1) : (size_t)n;
        
        // Сохраняем оригинальную строку
        char original[1024];
        if (len >= sizeof(original)) len = sizeof(original) - 1;
        strncpy(original, line, len);
        original[len] = '\0';

        // Переворачиваем строку
        reverse_string(line, len);
        line[len] = '\0';
        
        // Выводим результат в stdout
        printf("Transformed: %s\n", line);
        fflush(stdout);
        
        // Записываем в файл
        fprintf(out, "Original: %s\n", original);
        fprintf(out, "Transformed: %s\n", line);
        fflush(out);
    }

    free(line);
    fclose(out);
    return 0;
}