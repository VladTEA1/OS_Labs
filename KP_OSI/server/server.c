#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include "../shared/protocol.h"

SharedData* shared = NULL;
int mmap_fd = -1;
int running = 1;

// Обработчик Ctrl+C
void handle_signal(int sig) {
    printf("\nShutting down server...\n");
    running = 0;
}

// Функции синхронизации с мьютексами
void lock() {
    int ret = pthread_mutex_lock(&shared->mutex);
    if (ret != 0) {
        fprintf(stderr, "Ошибка блокировки мьютекса: %s\n", strerror(ret));
        exit(1);
    }
}

void unlock() {
    int ret = pthread_mutex_unlock(&shared->mutex);
    if (ret != 0) {
        fprintf(stderr, "Ошибка разблокировки мьютекса: %s\n", strerror(ret));
        exit(1);
    }
}

// Инициализация shared memory
int init_shared_memory() {
    // Пробуем открыть существующую shared memory
    mmap_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (mmap_fd < 0) {
        // Если не получилось, создаем файл mmap
        mmap_fd = open(MMAP_FILE, O_CREAT | O_RDWR, 0666);
        if (mmap_fd < 0) {
            perror("Failed to open mmap file");
            return -1;
        }
    }
    
    // Устанавливаем размер (увеличиваем для мьютекса)
    if (ftruncate(mmap_fd, MMAP_SIZE) < 0) {
        perror("ftruncate failed");
        close(mmap_fd);
        return -1;
    }
    
    // Маппируем в память
    shared = mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mmap_fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap failed");
        close(mmap_fd);
        return -1;
    }
    
    // Инициализируем если первый запуск
    if (shared->initialized != 12345) {
        memset(shared, 0, sizeof(SharedData));
        shared->player_count = 0;
        shared->game_count = 0;
        shared->initialized = 12345;
        
        // Инициализация мьютекса с атрибутами для shared memory
        pthread_mutexattr_init(&shared->mutex_attr);
        pthread_mutexattr_setpshared(&shared->mutex_attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&shared->mutex_attr, PTHREAD_MUTEX_ROBUST);
        pthread_mutex_init(&shared->mutex, &shared->mutex_attr);
        
        printf("Initialized new shared memory with POSIX mutex\n");
    } else {
        printf("Using existing shared memory\n");
        
        // Восстанавливаем мьютекс если он в неконсистентном состоянии
        int ret = pthread_mutex_consistent(&shared->mutex);
        if (ret == EINVAL) {
            // Мьютекс не требует восстановления
        } else if (ret != 0) {
            fprintf(stderr, "Warning: mutex recovery failed: %s\n", strerror(ret));
        }
    }
    
    return 0;
}

// Поиск игрока по логину (без изменений)
int find_player(const char* login) {
    for (int i = 0; i < shared->player_count; i++) {
        if (strcmp(shared->players[i].login, login) == 0) {
            return i;
        }
    }
    return -1;
}

// Добавление нового игрока (без изменений)
int add_player(const char* login) {
    int idx = find_player(login);
    if (idx >= 0) {
        // Игрок уже существует
        shared->players[idx].online = true;
        shared->players[idx].last_seen = time(NULL);
        return idx;
    }
    
    // Проверяем лимит
    if (shared->player_count >= MAX_PLAYERS) {
        return -1;
    }
    
    // Создаем нового игрока
    Player* p = &shared->players[shared->player_count];
    strncpy(p->login, login, MAX_LOGIN);
    p->wins = 0;
    p->losses = 0;
    p->online = true;
    p->game_id = -1;
    p->last_seen = time(NULL);
    p->ships_placed = false;
    
    shared->player_count++;
    return shared->player_count - 1;
}

// Поиск игры по имени (без изменений)
int find_game(const char* name) {
    for (int i = 0; i < shared->game_count; i++) {
        if (strcmp(shared->games[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Проверка возможности размещения корабля (без изменений)
int can_place_ship(int board[BOARD_SIZE][BOARD_SIZE], int x, int y, int size, ShipDirection dir) {
    // Проверка границ
    if (dir == DIR_HORIZONTAL) {
        if (x + size > BOARD_SIZE) return 0;
    } else {
        if (y + size > BOARD_SIZE) return 0;
    }
    
    // Проверка всех клеток корабля и области вокруг
    for (int i = 0; i < size; i++) {
        int cx = (dir == DIR_HORIZONTAL) ? x + i : x;
        int cy = (dir == DIR_VERTICAL) ? y + i : y;
        
        // Проверка самой клетки
        if (board[cx][cy] != CELL_EMPTY) {
            return 0;
        }
        
        // Проверка соседних клеток (включая диагонали)
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                int nx = cx + dx;
                int ny = cy + dy;
                
                if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE) {
                    if (board[nx][ny] == CELL_SHIP) {
                        return 0;  // Корабль касается другого корабля
                    }
                }
            }
        }
    }
    
    return 1;  // Можно разместить
}

// Размещение корабля на доске (без изменений)
void place_ship_on_board(int board[BOARD_SIZE][BOARD_SIZE], Ship* ship, 
                         int x, int y, int size, ShipDirection dir) {
    ship->size = size;
    ship->hits = 0;
    ship->sunk = false;
    ship->start_x = x;
    ship->start_y = y;
    ship->dir = dir;
    
    // Занимаем клетки
    for (int i = 0; i < size; i++) {
        int cx = (dir == DIR_HORIZONTAL) ? x + i : x;
        int cy = (dir == DIR_VERTICAL) ? y + i : y;
        
        board[cx][cy] = CELL_SHIP;
        ship->cells[i][0] = cx;
        ship->cells[i][1] = cy;
    }
}

// Обработка выстрела (без изменений)
int process_shot(Game* game, int player_num, int x, int y) {
    // Определяем доску цели
    int (*target_board)[BOARD_SIZE];
    Ship* target_ships;
    int* target_ships_count;
    
    if (player_num == 1) {
        target_board = game->board2;
        target_ships = game->ships2;
        target_ships_count = &game->ships_count2;
    } else {
        target_board = game->board1;
        target_ships = game->ships1;
        target_ships_count = &game->ships_count1;
    }
    
    // Проверяем координаты
    if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
        return -1;  // Неверные координаты
    }
    
    // Проверяем состояние клетки
    if (target_board[x][y] == CELL_HIT || 
        target_board[x][y] == CELL_MISS || 
        target_board[x][y] == CELL_SUNK) {
        return -2;  // Уже стреляли сюда
    }
    
    // Обработка выстрела
    if (target_board[x][y] == CELL_SHIP) {
        target_board[x][y] = CELL_HIT;
        
        // Ищем корабль и увеличиваем счетчик попаданий
        for (int s = 0; s < *target_ships_count; s++) {
            for (int c = 0; c < target_ships[s].size; c++) {
                if (target_ships[s].cells[c][0] == x && 
                    target_ships[s].cells[c][1] == y) {
                    target_ships[s].hits++;
                    
                    // Проверяем потопление
                    if (target_ships[s].hits == target_ships[s].size) {
                        target_ships[s].sunk = true;
                        
                        // Помечаем все клетки корабля как потопленные
                        for (int i = 0; i < target_ships[s].size; i++) {
                            int cx = target_ships[s].cells[i][0];
                            int cy = target_ships[s].cells[i][1];
                            target_board[cx][cy] = CELL_SUNK;
                        }
                        
                        return 2;  // Корабль потоплен
                    }
                    
                    return 1;  // Попадание
                }
            }
        }
        return 1;
    } else {
        target_board[x][y] = CELL_MISS;
        return 0;  // Промах
    }
}

// Проверка окончания игры (без изменений)
int check_game_over(Game* game) {
    // Проверяем все ли корабли первого игрока потоплены
    int all_sunk1 = 1;
    for (int i = 0; i < game->ships_count1; i++) {
        if (!game->ships1[i].sunk) {
            all_sunk1 = 0;
            break;
        }
    }
    
    if (all_sunk1) {
        game->winner = 2;
        game->status = GAME_FINISHED;
        return 2;
    }
    
    // Проверяем все ли корабли второго игрока потоплены
    int all_sunk2 = 1;
    for (int i = 0; i < game->ships_count2; i++) {
        if (!game->ships2[i].sunk) {
            all_sunk2 = 0;
            break;
        }
    }
    
    if (all_sunk2) {
        game->winner = 1;
        game->status = GAME_FINISHED;
        return 1;
    }
    
    return 0;  // Игра продолжается
}

// Очистка неактивных игроков (без изменений)
void cleanup_inactive_players() {
    time_t now = time(NULL);
    
    for (int i = 0; i < shared->player_count; i++) {
        if (shared->players[i].online && (now - shared->players[i].last_seen) > 300) {
            shared->players[i].online = false;
        }
    }
}

// Вывод статуса сервера (без изменений)
void print_server_status() {
    printf("\n=== Server Status ===\n");
    printf("Players: %d/%d (online: ", shared->player_count, MAX_PLAYERS);
    
    int online = 0;
    for (int i = 0; i < shared->player_count; i++) {
        if (shared->players[i].online) online++;
    }
    printf("%d)\n", online);
    
    printf("Games: %d/%d\n", shared->game_count, MAX_GAMES);
    
    // Статистика по играм
    int waiting = 0, placing = 0, playing = 0, finished = 0;
    for (int i = 0; i < shared->game_count; i++) {
        switch (shared->games[i].status) {
            case GAME_WAITING: waiting++; break;
            case GAME_PLACING_SHIPS: placing++; break;
            case GAME_PLAYING: playing++; break;
            case GAME_FINISHED: finished++; break;
        }
    }
    
    printf("Games status: waiting=%d, placing=%d, playing=%d, finished=%d\n",
           waiting, placing, playing, finished);
    
    if (shared->player_count > 0) {
        printf("Recent players:\n");
        for (int i = 0; i < shared->player_count && i < 5; i++) {
            printf("  %s: %dW/%dL %s\n", 
                   shared->players[i].login, 
                   shared->players[i].wins, 
                   shared->players[i].losses,
                   shared->players[i].online ? "online" : "offline");
        }
    }
}

// Основной цикл сервера
void server_loop() {
    // printf("Server started successfully!\n");
    // printf("Shared memory: %s\n", SHM_NAME);
    // printf("MMAP file: %s\n", MMAP_FILE);
    // printf("Synchronization: POSIX mutex (process-shared)\n");
    // printf("Press Ctrl+C to stop\n");
    
    int iteration = 0;
    
    while (running) {
        lock();
        
        cleanup_inactive_players();
        
        // Показываем статус каждые 10 секунд
        if (iteration % 20 == 0) {
            print_server_status();
        }
        
        // Проверяем игры на окончание
        for (int i = 0; i < shared->game_count; i++) {
            Game* game = &shared->games[i];
            
            if (game->status == GAME_PLAYING) {
                int result = check_game_over(game);
                if (result > 0) {
                    // Обновляем статистику игроков
                    int p1 = find_player(game->player1);
                    int p2 = find_player(game->player2);
                    
                    if (result == 1) {
                        if (p1 >= 0) shared->players[p1].wins++;
                        if (p2 >= 0) shared->players[p2].losses++;
                    } else {
                        if (p2 >= 0) shared->players[p2].wins++;
                        if (p1 >= 0) shared->players[p1].losses++;
                    }
                    
                    // Освобождаем игроков
                    if (p1 >= 0) shared->players[p1].game_id = -1;
                    if (p2 >= 0) shared->players[p2].game_id = -1;
                    
                    printf("Game '%s' finished. Winner: %s\n", 
                           game->name, 
                           result == 1 ? game->player1 : game->player2);
                }
            }
            
            // Проверяем готовность к началу игры
            if (game->status == GAME_PLACING_SHIPS) {
                if (game->ships_count1 == TOTAL_SHIPS && 
                    game->ships_count2 == TOTAL_SHIPS) {
                    game->status = GAME_PLAYING;
                    game->current_turn = 1;  // Первый ход у создателя игры
                    printf("Game '%s' started!\n", game->name);
                }
            }
        }
        
        unlock();
        
        usleep(500000);  // 500ms
        iteration++;
    }
}

// Основная функция
int main() {
    printf("=== Sea Battle Server ===\n");
    printf("Using MMAP for inter-process communication\n");
    
    // Настройка обработчиков сигналов
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Инициализация shared memory
    if (init_shared_memory() < 0) {
        fprintf(stderr, "Failed to initialize shared memory\n");
        return 1;
    }
    
    // Запуск основного цикла
    server_loop();
    
    // Очистка при завершении
    printf("\nCleaning up...\n");
    
    // Уничтожаем мьютекс
    pthread_mutex_destroy(&shared->mutex);
    pthread_mutexattr_destroy(&shared->mutex_attr);
    
    if (shared) {
        munmap(shared, MMAP_SIZE);
    }
    
    if (mmap_fd >= 0) {
        close(mmap_fd);
    }
    
    // Удаляем shared memory объекты
    shm_unlink(SHM_NAME);
    remove(MMAP_FILE);
    
    printf("Server stopped.\n");
    return 0;
}
