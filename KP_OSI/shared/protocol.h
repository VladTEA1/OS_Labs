#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <time.h>

// Конфигурация
#define SHM_NAME "/sea_battle_shm"
#define MMAP_FILE "/tmp/sea_battle.mmap"
#define MMAP_SIZE 65536  // 64KB

#define MAX_PLAYERS 20
#define MAX_GAMES 10
#define BOARD_SIZE 10
#define MAX_SHIPS 10
#define MAX_NAME 50
#define MAX_LOGIN 30

// Корабли по правилам: 1x4, 2x3, 3x2, 4x1
#define BATTLESHIP_COUNT 1    // Линкор (4 клетки)
#define CRUISER_COUNT 2       // Крейсера (3 клетки)
#define DESTROYER_COUNT 3     // Эсминцы (2 клетки)
#define BOAT_COUNT 4          // Катера (1 клетка)
#define TOTAL_SHIPS 10        // Всего кораблей

// Типы клеток
typedef enum {
    CELL_EMPTY = 0,
    CELL_SHIP = 1,
    CELL_HIT = 2,
    CELL_MISS = 3,
    CELL_SUNK = 4
} CellType;

// Статус игры
typedef enum {
    GAME_WAITING = 0,        // Ожидание второго игрока
    GAME_PLACING_SHIPS = 1,  // Расстановка кораблей
    GAME_PLAYING = 2,        // Игра идет
    GAME_FINISHED = 3        // Игра завершена
} GameStatus;

// Направление корабля
typedef enum {
    DIR_HORIZONTAL = 0,
    DIR_VERTICAL = 1
} ShipDirection;

// Структура игрока
typedef struct {
    char login[MAX_LOGIN];
    int wins;
    int losses;
    bool online;
    int game_id;            // ID игры, в которой участвует (-1 если нет)
    time_t last_seen;
    bool ships_placed;      // Расставил ли корабли
} Player;

// Структура корабля
typedef struct {
    int size;               // Размер (1-4)
    int hits;               // Количество попаданий
    bool sunk;              // Потоплен ли
    int cells[4][2];        // Координаты ячеек [размер][x,y]
    int start_x;            // Начальная координата X
    int start_y;            // Начальная координата Y
    ShipDirection dir;      // Направление
} Ship;

// Структура игры
typedef struct {
    int id;
    char name[MAX_NAME];
    char player1[MAX_LOGIN];
    char player2[MAX_LOGIN];
    
    // Доски игроков
    int board1[BOARD_SIZE][BOARD_SIZE];  // Доска первого игрока
    int board2[BOARD_SIZE][BOARD_SIZE];  // Доска второго игрока
    
    // Корабли игроков
    Ship ships1[TOTAL_SHIPS];
    Ship ships2[TOTAL_SHIPS];
    int ships_count1;      // Количество расставленных кораблей
    int ships_count2;
    
    GameStatus status;
    int current_turn;      // 1 - ход первого, 2 - ход второго
    int winner;            // 0 - нет, 1 - player1, 2 - player2
    time_t last_move;
} Game;

// Главная структура shared memory
typedef struct {
    Player players[MAX_PLAYERS];
    Game games[MAX_GAMES];
    
    int player_count;
    int game_count;
    
    // Мьютекс для синхронизации
    int mutex;
    int initialized;
} SharedData;

#endif // PROTOCOL_Hй