#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <termios.h>
#include <pthread.h>
#include <errno.h>
#include "../shared/protocol.h"

SharedData* shared = NULL;
int mmap_fd = -1;
char my_login[MAX_LOGIN] = "";
int my_game_id = -1;
int my_player_num = 0;

// Корабли для расстановки (по правилам)
int ships_to_place[] = {4, 3, 3, 2, 2, 2, 1, 1, 1, 1};
int current_ship_index = 0;

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

// Подключение к shared memory
int connect_to_server() {
    // Пробуем подключиться через shm_open
    mmap_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (mmap_fd < 0) {
        // Если не получилось, пробуем через файл
        mmap_fd = open(MMAP_FILE, O_RDWR, 0666);
        if (mmap_fd < 0) {
            printf("ERROR: Server is not running!\n");
            printf("Please start the server first: cd server && ./sea_battle_server\n");
            return -1;
        }
    }
    
    // Маппируем shared memory
    shared = mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mmap_fd, 0);
    if (shared == MAP_FAILED) {
        perror("Failed to map shared memory");
        close(mmap_fd);
        return -1;
    }
    
    // Проверяем, инициализирована ли shared memory
    if (shared->initialized != 12345) {
        printf("Shared memory not properly initialized by server\n");
        munmap(shared, MMAP_SIZE);
        close(mmap_fd);
        return -1;
    }
    
    return 0;
}

// Отображение игрового поля (без изменений, как в оригинале)
void print_board(int board[BOARD_SIZE][BOARD_SIZE], int show_ships) {
    printf("   ");
    for (int i = 0; i < BOARD_SIZE; i++) {
        printf("%2d ", i);
    }
    printf("\n");
    
    for (int y = 0; y < BOARD_SIZE; y++) {
        printf("%2d ", y);
        for (int x = 0; x < BOARD_SIZE; x++) {
            char symbol = ' ';
            switch (board[x][y]) {
                case CELL_EMPTY:
                    symbol = '.';
                    break;
                case CELL_SHIP:
                    symbol = show_ships ? 'S' : '.';
                    break;
                case CELL_HIT:
                    symbol = 'X';
                    break;
                case CELL_MISS:
                    symbol = 'O';
                    break;
                case CELL_SUNK:
                    symbol = '#';
                    break;
            }
            printf(" %c ", symbol);
        }
        printf("\n");
    }
}

// Проверка возможности размещения корабля (без изменений)
int can_place_ship_here(int board[BOARD_SIZE][BOARD_SIZE], int x, int y, int size, ShipDirection dir) {
    // Проверка границ
    if (dir == DIR_HORIZONTAL) {
        if (x + size > BOARD_SIZE) {
            return 0;
        }
    } else {
        if (y + size > BOARD_SIZE) {
            return 0;
        }
    }
    
    // Проверка клеток и области вокруг
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
                        return 0;  // Корабли касаются
                    }
                }
            }
        }
    }
    
    return 1;  // Можно разместить
}

// Логин игрока
int login_player() {
    printf("Enter your login (3-29 characters): ");
    fgets(my_login, MAX_LOGIN, stdin);
    my_login[strcspn(my_login, "\n")] = '\0';
    
    if (strlen(my_login) < 3) {
        printf("Login is too short\n");
        return -1;
    }
    
    // Подключаемся к серверу
    if (connect_to_server() < 0) {
        return -1;
    }
    
    lock();
    
    // Проверяем существующего игрока
    int player_idx = -1;
    for (int i = 0; i < shared->player_count; i++) {
        if (strcmp(shared->players[i].login, my_login) == 0) {
            player_idx = i;
            break;
        }
    }
    
    if (player_idx >= 0) {
        // Игрок уже существует
        shared->players[player_idx].online = true;
        shared->players[player_idx].last_seen = time(NULL);
        my_game_id = shared->players[player_idx].game_id;
        printf("Welcome back, %s!\n", my_login);
    } else if (shared->player_count < MAX_PLAYERS) {
        // Новый игрок
        Player* p = &shared->players[shared->player_count];
        strcpy(p->login, my_login);
        p->wins = 0;
        p->losses = 0;
        p->online = true;
        p->game_id = -1;
        p->last_seen = time(NULL);
        p->ships_placed = false;
        
        player_idx = shared->player_count;
        shared->player_count++;
        
        printf("Welcome, %s! You are player #%d\n", my_login, shared->player_count);
    } else {
        printf("Server is full (maximum %d players)\n", MAX_PLAYERS);
        unlock();
        return -1;
    }
    
    unlock();
    return 0;
}

// Создание новой игры
void create_game() {
    char name[MAX_NAME];
    printf("Enter game name: ");
    fgets(name, MAX_NAME, stdin);
    name[strcspn(name, "\n")] = '\0';
    
    lock();
    
    // Проверяем что имя не занято
    for (int i = 0; i < shared->game_count; i++) {
        if (strcmp(shared->games[i].name, name) == 0) {
            printf("Game name '%s' is already taken\n", name);
            unlock();
            return;
        }
    }
    
    // Проверяем лимит игр
    if (shared->game_count >= MAX_GAMES) {
        printf("Maximum number of games reached\n");
        unlock();
        return;
    }
    
    // Создаем новую игру
    Game* game = &shared->games[shared->game_count];
    game->id = shared->game_count;
    strcpy(game->name, name);
    strcpy(game->player1, my_login);
    game->player2[0] = '\0';
    
    // Инициализация игры
    game->status = GAME_WAITING;
    game->current_turn = 1;
    game->winner = 0;
    game->last_move = time(NULL);
    game->ships_count1 = 0;
    game->ships_count2 = 0;
    
    // Очищаем игровые поля
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            game->board1[i][j] = CELL_EMPTY;
            game->board2[i][j] = CELL_EMPTY;
        }
    }
    
    // Обновляем информацию об игроке
    for (int i = 0; i < shared->player_count; i++) {
        if (strcmp(shared->players[i].login, my_login) == 0) {
            shared->players[i].game_id = game->id;
            break;
        }
    }
    
    my_game_id = game->id;
    my_player_num = 1;
    
    shared->game_count++;
    
    printf("Game '%s' created successfully! ID: %d\n", name, game->id);
    printf("You are Player 1. Waiting for opponent...\n");
    
    unlock();
}

// Присоединение к существующей игре
void join_game() {
    lock();
    
    printf("\n=== Available Games ===\n");
    int available = 0;
    for (int i = 0; i < shared->game_count; i++) {
        if (shared->games[i].status == GAME_WAITING) {
            printf("ID: %d - '%s' created by %s\n", 
                   i, shared->games[i].name, shared->games[i].player1);
            available++;
        }
    }
    
    if (available == 0) {
        printf("No games available to join\n");
        unlock();
        return;
    }
    
    unlock();
    
    printf("\nEnter game ID to join: ");
    int game_id;
    if (scanf("%d", &game_id) != 1) {
        printf("Invalid input\n");
        while (getchar() != '\n');
        return;
    }
    getchar();  // Убираем символ новой строки
    
    lock();
    
    if (game_id < 0 || game_id >= shared->game_count) {
        printf("Invalid game ID\n");
        unlock();
        return;
    }
    
    Game* game = &shared->games[game_id];
    
    if (game->status != GAME_WAITING) {
        printf("This game is not available\n");
        unlock();
        return;
    }
    
    if (strlen(game->player2) > 0) {
        printf("This game already has two players\n");
        unlock();
        return;
    }
    
    // Присоединяемся к игре
    strcpy(game->player2, my_login);
    game->status = GAME_PLACING_SHIPS;
    
    // Обновляем информацию об игроке
    for (int i = 0; i < shared->player_count; i++) {
        if (strcmp(shared->players[i].login, my_login) == 0) {
            shared->players[i].game_id = game_id;
            break;
        }
    }
    
    my_game_id = game_id;
    my_player_num = 2;
    
    printf("Successfully joined game '%s'!\n", game->name);
    printf("You are Player 2. Get ready to place your ships!\n");
    
    unlock();
}

// Расстановка кораблей
void place_ships() {
    printf("\n=== PLACING SHIPS ===\n");
    printf("You need to place 10 ships according to the rules:\n");
    printf("• 1 battleship (4 cells)\n");
    printf("• 2 cruisers (3 cells each)\n");
    printf("• 3 destroyers (2 cells each)\n");
    printf("• 4 boats (1 cell each)\n");
    printf("Ships cannot touch each other, even diagonally!\n");
    
    current_ship_index = 0;
    
    while (current_ship_index < TOTAL_SHIPS) {
        int ship_size = ships_to_place[current_ship_index];
        
        lock();
        Game* game = &shared->games[my_game_id];
        
        // Получаем нашу доску
        int (*my_board)[BOARD_SIZE];
        int* my_ships_count;
        
        if (my_player_num == 1) {
            my_board = game->board1;
            my_ships_count = &game->ships_count1;
        } else {
            my_board = game->board2;
            my_ships_count = &game->ships_count2;
        }
        
        // Показываем текущую доску
        printf("\nYour current board (ship size: %d):\n", ship_size);
        print_board(my_board, 1);
        
        printf("\nRemaining ships to place: ");
        for (int i = current_ship_index; i < TOTAL_SHIPS; i++) {
            printf("%d ", ships_to_place[i]);
        }
        printf("\n");
        
        unlock();
        
        // Запрашиваем координаты
        printf("Enter coordinates (x y) and direction (0-horizontal, 1-vertical): ");
        int x, y, dir_input;
        if (scanf("%d %d %d", &x, &y, &dir_input) != 3) {
            printf("Invalid input. Please enter three numbers.\n");
            while (getchar() != '\n');  // Очищаем буфер
            continue;
        }
        getchar();  // Убираем символ новой строки
        
        if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
            printf("Coordinates must be between 0 and %d\n", BOARD_SIZE - 1);
            continue;
        }
        
        if (dir_input != 0 && dir_input != 1) {
            printf("Direction must be 0 (horizontal) or 1 (vertical)\n");
            continue;
        }
        
        ShipDirection dir = (dir_input == 0) ? DIR_HORIZONTAL : DIR_VERTICAL;
        
        lock();
        
        // Проверяем возможность размещения
        if (!can_place_ship_here(my_board, x, y, ship_size, dir)) {
            printf("Cannot place ship here. Ships cannot touch!\n");
            unlock();
            continue;
        }
        
        // Размещаем корабль
        Ship* ship;
        if (my_player_num == 1) {
            ship = &game->ships1[*my_ships_count];
        } else {
            ship = &game->ships2[*my_ships_count];
        }
        
        // Занимаем клетки на доске
        for (int i = 0; i < ship_size; i++) {
            int cx = (dir == DIR_HORIZONTAL) ? x + i : x;
            int cy = (dir == DIR_VERTICAL) ? y + i : y;
            
            my_board[cx][cy] = CELL_SHIP;
            ship->cells[i][0] = cx;
            ship->cells[i][1] = cy;
        }
        
        ship->size = ship_size;
        ship->hits = 0;
        ship->sunk = false;
        ship->start_x = x;
        ship->start_y = y;
        ship->dir = dir;
        
        (*my_ships_count)++;
        
        unlock();
        
        printf("Ship placed successfully!\n");
        current_ship_index++;
        
        // Проверяем началась ли игра
        lock();
        if (shared->games[my_game_id].status == GAME_PLAYING) {
            printf("\n=== GAME STARTS! ===\n");
            unlock();
            return;
        }
        unlock();
    }
    
    printf("\nAll ships placed! Waiting for opponent...\n");
}

// Игровой ход
void play_turn() {
    lock();
    
    if (my_game_id >= shared->game_count) {
        printf("Game not found\n");
        unlock();
        return;
    }
    
    Game* game = &shared->games[my_game_id];
    
    if (game->status != GAME_PLAYING) {
        printf("Game is not active\n");
        unlock();
        return;
    }
    
    printf("\n=== Game: %s ===\n", game->name);
    printf("Player 1: %s\n", game->player1);
    printf("Player 2: %s\n", game->player2);
    printf("Current turn: Player %d (%s)\n", 
           game->current_turn,
           game->current_turn == 1 ? game->player1 : game->player2);
    
    // Показываем наше поле
    printf("\nYour ships:\n");
    print_board(my_player_num == 1 ? game->board1 : game->board2, 1);
    
    // Показываем поле противника
    printf("\nOpponent's field (your shots):\n");
    print_board(my_player_num == 1 ? game->board2 : game->board1, 0);
    
    if (game->current_turn == my_player_num) {
        unlock();
        
        printf("\n=== YOUR TURN! ===\n");
        
        while (1) {
            printf("Enter coordinates to shoot (x y): ");
            int x, y;
            if (scanf("%d %d", &x, &y) != 2) {
                printf("Invalid input. Please enter two numbers.\n");
                while (getchar() != '\n');
                continue;
            }
            getchar();
            
            if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
                printf("Coordinates must be between 0 and %d\n", BOARD_SIZE - 1);
                continue;
            }
            
            lock();
            
            // Определяем доску цели
            int (*target_board)[BOARD_SIZE];
            Ship* target_ships;
            int* target_ships_count;
            
            if (my_player_num == 1) {
                target_board = game->board2;
                target_ships = game->ships2;
                target_ships_count = &game->ships_count2;
            } else {
                target_board = game->board1;
                target_ships = game->ships1;
                target_ships_count = &game->ships_count1;
            }
            
            // Проверяем не стреляли ли уже сюда
            if (target_board[x][y] == CELL_HIT || 
                target_board[x][y] == CELL_MISS || 
                target_board[x][y] == CELL_SUNK) {
                printf("You already shot here! Try different coordinates.\n");
                unlock();
                continue;
            }
            
            // Выстрел
            if (target_board[x][y] == CELL_SHIP) {
                printf("HIT!\n");
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
                                
                                printf("SHIP SUNK!\n");
                            }
                            break;
                        }
                    }
                }
                
                // Проверяем победу
                int ships_left = 0;
                for (int s = 0; s < *target_ships_count; s++) {
                    if (!target_ships[s].sunk) {
                        ships_left = 1;
                        break;
                    }
                }
                
                if (ships_left == 0) {
                    printf("\n=== VICTORY! You destroyed all enemy ships! ===\n");
                    
                    game->status = GAME_FINISHED;
                    game->winner = my_player_num;
                    
                    // Обновляем статистику
                    for (int i = 0; i < shared->player_count; i++) {
                        if (strcmp(shared->players[i].login, my_login) == 0) {
                            shared->players[i].wins++;
                        }
                        if (strcmp(shared->players[i].login, 
                                   my_player_num == 1 ? game->player2 : game->player1) == 0) {
                            shared->players[i].losses++;
                        }
                    }
                    
                    // Освобождаем игроков
                    for (int i = 0; i < shared->player_count; i++) {
                        if (strcmp(shared->players[i].login, my_login) == 0 ||
                            strcmp(shared->players[i].login, 
                                   my_player_num == 1 ? game->player2 : game->player1) == 0) {
                            shared->players[i].game_id = -1;
                        }
                    }
                    
                    my_game_id = -1;
                    my_player_num = 0;
                } else {
                    printf("You get another turn!\n");
                }
            } else {
                printf("MISS!\n");
                target_board[x][y] = CELL_MISS;
                game->current_turn = (my_player_num == 1) ? 2 : 1;
                printf("Turn passes to opponent\n");
            }
            
            game->last_move = time(NULL);
            unlock();
            break;
        }
    } else {
        unlock();
        printf("\nWaiting for opponent's move...\n");
        printf("Press Enter to refresh");
        getchar();
    }
}

// Просмотр списка игр
void list_games() {
    lock();
    
    printf("\n=== Games List ===\n");
    
    if (shared->game_count == 0) {
        printf("No active games\n");
    } else {
        for (int i = 0; i < shared->game_count; i++) {
            Game* game = &shared->games[i];
            const char* status;
            
            switch (game->status) {
                case GAME_WAITING:
                    status = "waiting for player 2";
                    break;
                case GAME_PLACING_SHIPS:
                    status = "placing ships";
                    break;
                case GAME_PLAYING:
                    status = "in progress";
                    break;
                case GAME_FINISHED:
                    status = "finished";
                    break;
                default:
                    status = "unknown";
            }
            
            printf("%d. '%s' - %s vs %s - %s\n",
                   i, game->name, game->player1,
                   strlen(game->player2) > 0 ? game->player2 : "waiting",
                   status);
        }
    }
    
    unlock();
}

// Просмотр статистики
void show_stats() {
    lock();
    
    for (int i = 0; i < shared->player_count; i++) {
        if (strcmp(shared->players[i].login, my_login) == 0) {
            printf("\n=== Your Statistics ===\n");
            printf("Player: %s\n", shared->players[i].login);
            printf("Wins: %d\n", shared->players[i].wins);
            printf("Losses: %d\n", shared->players[i].losses);
            printf("Total games: %d\n", 
                   shared->players[i].wins + shared->players[i].losses);
            
            if (shared->players[i].wins + shared->players[i].losses > 0) {
                float win_rate = (float)shared->players[i].wins / 
                                (shared->players[i].wins + shared->players[i].losses) * 100;
                printf("Win rate: %.1f%%\n", win_rate);
            }
            break;
        }
    }
    
    unlock();
}

// Главное меню (без изменений, как в оригинале)
void main_menu() {
    int choice;
    
    while (1) {
        printf("\n=== Sea Battle ===\n");
        printf("Player: %s\n", my_login);
        
        if (my_game_id >= 0) {
            lock();
            Game* game = &shared->games[my_game_id];
            
            printf("Game: '%s' (Player %d)\n", game->name, my_player_num);
            printf("Status: ");
            
            switch (game->status) {
                case GAME_WAITING:
                    printf("Waiting for opponent\n");
                    break;
                case GAME_PLACING_SHIPS:
                    printf("Placing ships (%d/10 placed)\n", 
                           my_player_num == 1 ? game->ships_count1 : game->ships_count2);
                    break;
                case GAME_PLAYING:
                    printf("Playing - %s's turn\n", 
                           game->current_turn == 1 ? game->player1 : game->player2);
                    break;
                case GAME_FINISHED:
                    printf("Finished - Winner: %s\n", 
                           game->winner == 1 ? game->player1 : game->player2);
                    break;
            }
            
            unlock();
            
            if (my_game_id >= 0) {
                printf("\n1. Play/Continue\n");
                printf("2. View game info\n");
                printf("3. Leave game\n");
                printf("4. Exit\n");
            }
        } else {
            printf("Status: Not in game\n");
            printf("\n1. Create new game\n");
            printf("2. Join existing game\n");
            printf("3. List all games\n");
            printf("4. Show statistics\n");
            printf("5. Exit\n");
        }
        
        printf("\nChoice: ");
        
        if (scanf("%d", &choice) != 1) {
            printf("Invalid input\n");
            while (getchar() != '\n');
            continue;
        }
        getchar();
        
        if (my_game_id >= 0) {
            lock();
            int game_status = shared->games[my_game_id].status;
            unlock();
            
            switch (choice) {
                case 1:
                    if (game_status == GAME_PLACING_SHIPS) {
                        place_ships();
                    } else if (game_status == GAME_PLAYING) {
                        play_turn();
                    } else {
                        printf("Game is not active\n");
                    }
                    break;
                    
                case 2:
                    list_games();
                    break;
                    
                case 3:
                    // Покидаем игру
                    lock();
                    for (int i = 0; i < shared->player_count; i++) {
                        if (strcmp(shared->players[i].login, my_login) == 0) {
                            shared->players[i].game_id = -1;
                            break;
                        }
                    }
                    unlock();
                    
                    my_game_id = -1;
                    my_player_num = 0;
                    printf("Left the game\n");
                    break;
                    
                case 4:
                    return;
                    
                default:
                    printf("Invalid choice\n");
            }
        } else {
            switch (choice) {
                case 1:
                    create_game();
                    break;
                    
                case 2:
                    join_game();
                    break;
                    
                case 3:
                    list_games();
                    break;
                    
                case 4:
                    show_stats();
                    break;
                    
                case 5:
                    return;
                    
                default:
                    printf("Invalid choice\n");
            }
        }
        
        // Обновляем время последней активности
        lock();
        for (int i = 0; i < shared->player_count; i++) {
            if (strcmp(shared->players[i].login, my_login) == 0) {
                shared->players[i].last_seen = time(NULL);
                break;
            }
        }
        unlock();
    }
}

// Основная функция клиента
int main() {
    printf("=== Sea Battle Client ===\n");
    printf("Using MMAP for communication with server\n");
    printf("Synchronization: POSIX mutexes\n\n");
    
    // Логин
    if (login_player() != 0) {
        return 1;
    }
    
    // Главное меню
    main_menu();
    
    // Выход
    printf("\nGoodbye, %s!\n", my_login);
    
    // Помечаем игрока как оффлайн
    lock();
    for (int i = 0; i < shared->player_count; i++) {
        if (strcmp(shared->players[i].login, my_login) == 0) {
            shared->players[i].online = false;
            break;
        }
    }
    unlock();
    
    // Очистка ресурсов
    if (shared) {
        munmap(shared, MMAP_SIZE);
    }
    
    if (mmap_fd >= 0) {
        close(mmap_fd);
    }
    
    return 0;
}
