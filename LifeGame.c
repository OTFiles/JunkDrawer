/*
编译选项说明（支持 Android 环境）：

1. 通用编译（无优化）：
   gcc -o LifeGame LifeGame.c -lncurses -pthread -O2

2. ARM32优化编译（Android 环境）：
   # 基础编译（Android 环境）
   gcc -marm -march=armv7-a -mtune=cortex-a53 -mfpu=neon -mfloat-abi=softfp \
       -O3 -DARM_OPTIMIZED -o LifeGame_arm32 LifeGame.c -lncurses -pthread
   
   # Clang PGO优化（Android 环境）
   clang -marm -march=armv7-a -mtune=cortex-a53 -mfpu=neon -mfloat-abi=softfp \
         -fprofile-instr-generate -O3 -DARM_OPTIMIZED -o LifeGame_arm32_pgo LifeGame.c -lncurses -pthread
   ./LifeGame_arm32_pgo
   llvm-profdata merge -output=default.profdata *.profraw
   clang -marm -march=armv7-a -mtune=cortex-a53 -mfpu=neon -mfloat-abi=softfp \
         -fprofile-instr-use=default.profdata -O3 -DARM_OPTIMIZED -o LifeGame_arm32_optimized LifeGame.c -lncurses -pthread
   
   # GCC PGO优化（Android 环境）
   gcc -marm -march=armv7-a -mtune=cortex-a53 -mfpu=neon -mfloat-abi=softfp \
       -fprofile-generate -O3 -DARM_OPTIMIZED -o LifeGame_arm32_pgo LifeGame.c -lncurses -pthread
   ./LifeGame_arm32_pgo
   gcc -marm -march=armv7-a -mtune=cortex-a53 -mfpu=neon -mfloat-abi=softfp \
       -fprofile-use -O3 -DARM_OPTIMIZED -o LifeGame_arm32_optimized LifeGame.c -lncurses -pthread

3. ARM64优化编译（Android 环境）：
   # 基础编译
   gcc -O3 -DARM64_OPTIMIZED -o LifeGame_arm64 LifeGame.c -lncurses -pthread
   
   # Clang PGO优化
   clang -O3 -DARM64_OPTIMIZED -march=armv8-a -mtune=cortex-a72 \
         -fprofile-instr-generate -o LifeGame_arm64_pgo LifeGame.c -lncurses -pthread
   ./LifeGame_arm64_pgo
   llvm-profdata merge -output=default.profdata *.profraw
   clang -O3 -DARM64_OPTIMIZED -march=armv8-a -mtune=cortex-a72 \
         -fprofile-instr-use=default.profdata -o LifeGame_arm64_optimized LifeGame.c -lncurses -pthread
   
   # GCC PGO优化
   gcc -O3 -DARM64_OPTIMIZED -march=armv8-a -mtune=cortex-a72 \
       -fprofile-generate -o LifeGame_arm64_pgo LifeGame.c -lncurses -pthread
   ./LifeGame_arm64_pgo
   gcc -O3 -DARM64_OPTIMIZED -march=armv8-a -mtune=cortex-a72 \
       -fprofile-use -o LifeGame_arm64_optimized LifeGame.c -lncurses -pthread
   
   # 针对树莓派4（Cortex-A72）
   gcc -O3 -DARM64_OPTIMIZED -mcpu=cortex-a72 -o LifeGame_rpi4 LifeGame.c -lncurses -pthread

4. x86_64优化编译（通用 Linux 环境）：
   # 基础编译
   gcc -m64 -march=x86-64 -mtune=znver3 -O3 -DX64_OPTIMIZED \
       -o LifeGame_x64 LifeGame.c -lncurses -pthread
   
   # PGO优化（GCC）
   gcc -m64 -march=haswell -flto -fprofile-generate -O3 -DX64_OPTIMIZED -mpopcnt \
       -o LifeGame_x64_pgo LifeGame.c -lncurses -pthread
   ./LifeGame_x64_pgo
   gcc -m64 -march=haswell -flto -fprofile-use -fprofile-correction -O3 -DX64_OPTIMIZED -mpopcnt \
       -o LifeGame_x64_optimized LifeGame.c -lncurses -pthread
   
   # PGO优化（Clang）
   clang -m64 -march=haswell -flto -fprofile-instr-generate -O3 -DX64_OPTIMIZED -mpopcnt \
         -o LifeGame_x64_pgo LifeGame.c -lncurses -pthread
   ./LifeGame_x64_pgo
   llvm-profdata merge -output=default.profdata *.profraw
   clang -m64 -march=haswell -flto -fprofile-instr-use=default.profdata -O3 -DX64_OPTIMIZED -mpopcnt \
         -o LifeGame_x64_optimized LifeGame.c -lncurses -pthread
*/

#define _XOPEN_SOURCE 700
#include <curses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <stdbool.h>
#include <arm_neon.h>

// 速度等级定义
#define MIN_SPEED_LEVEL 1
#define MAX_SPEED_LEVEL 10
#define DEFAULT_SPEED_LEVEL 5

// 根据架构选择优化配置
#if defined(ARM_OPTIMIZED)
    // ARM32优化配置
    #define CHUNK_SIZE 32
    typedef uint32_t BitmapType;
    #define BITS_PER_UNIT 32
    #define BITMAP_SIZE ((CHUNK_SIZE * CHUNK_SIZE + BITS_PER_UNIT - 1) / BITS_PER_UNIT)
#elif defined(ARM64_OPTIMIZED)
    // ARM64优化配置
    #define CHUNK_SIZE 64
    typedef uint64_t BitmapType;
    #define BITS_PER_UNIT 64
    #define BITMAP_SIZE ((CHUNK_SIZE * CHUNK_SIZE + BITS_PER_UNIT - 1) / BITS_PER_UNIT)
    #define NEON_OPTIMIZATION 1
#elif defined(X64_OPTIMIZED)
    // x86_64优化配置
    #define CHUNK_SIZE 64
    typedef uint64_t BitmapType;
    #define BITS_PER_UNIT 64
    #define BITMAP_SIZE ((CHUNK_SIZE * CHUNK_SIZE + BITS_PER_UNIT - 1) / BITS_PER_UNIT)
    #define POPCNT_OPTIMIZATION 1
#else
    // 通用配置
    #define CHUNK_SIZE 32
    typedef uint32_t BitmapType;
    #define BITS_PER_UNIT 32
    #define BITMAP_SIZE ((CHUNK_SIZE * CHUNK_SIZE + BITS_PER_UNIT - 1) / BITS_PER_UNIT)
#endif

// 优化邻居计算
static const int neighbor_offsets[8][2] = {
    {-1, -1}, {0, -1}, {1, -1},
    {-1,  0},           {1,  0},
    {-1,  1}, {0,  1}, {1,  1}
};

// 模式枚举
typedef enum {
    DESIGN,
    COMMAND,
    PLAY
} Mode;

// 命令结果枚举
typedef enum {
    SUCCESS,
    ERROR,
    CANCEL
} CommandResult;

// 块结构
typedef struct Chunk {
    BitmapType bitmap[BITMAP_SIZE];
    bool dirty;
    int live_count;
} Chunk;

// 坐标对
typedef struct {
    int first;
    int second;
} Pair;

// 更新三元组
typedef struct {
    int x;
    int y;
    bool alive;
} Update;

// 哈希表条目
typedef struct ChunkEntry {
    int key;
    Chunk *chunk;
    SLIST_ENTRY(ChunkEntry) entries;
} ChunkEntry;

// 行哈希表
typedef SLIST_HEAD(ChunkHead, ChunkEntry) ChunkRow;
typedef struct RowEntry {
    int key;
    ChunkRow row;
    SLIST_ENTRY(RowEntry) entries;
} RowEntry;

// 世界哈希表
typedef SLIST_HEAD(WorldHead, RowEntry) World;

// 游戏状态结构
typedef struct {
    Mode mode;
    int rows, cols;
    World world;
    int live_cell_count;
    int viewport_x;
    int viewport_y;
    int cursor_screen_x;
    int cursor_screen_y;
    int prev_cursor_screen_x;
    int prev_cursor_screen_y;
    bool viewport_changed;
    bool need_full_refresh;
    char *command_str;
    bool precompute;
    int precompute_rounds;
    bool running;
    Pair *dirty_chunks;
    int dirty_count;
    int dirty_capacity;
    Pair *positions_to_check;
    int positions_count;
    int positions_capacity;
    Update *updates;
    int updates_count;
    int updates_capacity;
    int prev_rows, prev_cols; // 保存上一次的终端尺寸
    int speed_level;          // 速度等级 (1-10)
    int skip_frames;          // 跳过的帧数（用于高速模式）
} GameState;

// 函数声明
void init_game(GameState *state, int argc, char** argv);
void free_game(GameState *state);
Chunk* get_chunk_if_exists(GameState *state, int world_x, int world_y);
Chunk* get_chunk(GameState *state, int world_x, int world_y);
bool peek_cell(GameState *state, int world_x, int world_y);
void set_cell(GameState *state, int world_x, int world_y, bool alive);
void compute_generation(GameState *state);
void show_loading(GameState *state);
void draw_chunk(GameState *state, int chunk_x, int chunk_y, Chunk *chunk);
void draw_cursor(GameState *state);
void draw_all_visible_chunks(GameState *state);
CommandResult save_world(GameState *state, const char* filename);
CommandResult load_world(GameState *state, const char* filename);
void design_mode(GameState *state);
void command_mode(GameState *state);
void play_mode(GameState *state);
void resize_array(void **array, int *capacity, int count, size_t element_size);
void add_to_dirty_chunks(GameState *state, int chunk_x, int chunk_y);
void add_to_positions(GameState *state, int x, int y);
void add_to_updates(GameState *state, int x, int y, bool alive);
void free_world(World *world);
uint64_t get_time_ms();
void adjust_speed(GameState *state, int change);

// 获取当前时间（毫秒）
uint64_t get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
}

// 调整速度
void adjust_speed(GameState *state, int change) {
    state->speed_level += change;
    
    // 确保速度在有效范围内
    if (state->speed_level < MIN_SPEED_LEVEL) state->speed_level = MIN_SPEED_LEVEL;
    if (state->speed_level > MAX_SPEED_LEVEL) state->speed_level = MAX_SPEED_LEVEL;
    
    // 重置跳帧计数器
    state->skip_frames = 0;
}

// 调整动态数组大小
void resize_array(void **array, int *capacity, int count, size_t element_size) {
    if (count >= *capacity) {
        int new_capacity = *capacity == 0 ? 64 : *capacity * 2;
        void *new_array = realloc(*array, new_capacity * element_size);
        if (!new_array) return;
        *array = new_array;
        *capacity = new_capacity;
    }
}

// 添加脏块
void add_to_dirty_chunks(GameState *state, int chunk_x, int chunk_y) {
    resize_array((void**)&state->dirty_chunks, &state->dirty_capacity, state->dirty_count, sizeof(Pair));
    state->dirty_chunks[state->dirty_count].first = chunk_x;
    state->dirty_chunks[state->dirty_count].second = chunk_y;
    state->dirty_count++;
}

// 添加位置
void add_to_positions(GameState *state, int x, int y) {
    resize_array((void**)&state->positions_to_check, &state->positions_capacity, state->positions_count, sizeof(Pair));
    state->positions_to_check[state->positions_count].first = x;
    state->positions_to_check[state->positions_count].second = y;
    state->positions_count++;
}

// 添加更新
void add_to_updates(GameState *state, int x, int y, bool alive) {
    resize_array((void**)&state->updates, &state->updates_capacity, state->updates_count, sizeof(Update));
    state->updates[state->updates_count].x = x;
    state->updates[state->updates_count].y = y;
    state->updates[state->updates_count].alive = alive;
    state->updates_count++;
}

// 块操作：获取位值
bool chunk_get_bit(const Chunk *chunk, int x, int y) {
    int64_t pos = (int64_t)y * CHUNK_SIZE + x;
    if (pos < 0 || pos >= CHUNK_SIZE * CHUNK_SIZE) return false;
    size_t idx = (size_t)pos / BITS_PER_UNIT;
    return (chunk->bitmap[idx] >> (pos % BITS_PER_UNIT)) & 1;
}

// 块操作：设置位值
void chunk_set_bit(Chunk *chunk, int x, int y, bool value) {
    int64_t pos = (int64_t)y * CHUNK_SIZE + x;
    if (pos < 0 || pos >= CHUNK_SIZE * CHUNK_SIZE) return;
    size_t idx = (size_t)pos / BITS_PER_UNIT;
    BitmapType mask = (BitmapType)1 << (pos % BITS_PER_UNIT);
    
    bool current = (chunk->bitmap[idx] & mask) != 0;
    if (current == value) return;
    
    if (value) {
        chunk->bitmap[idx] |= mask;
        chunk->live_count++;
    } else {
        chunk->bitmap[idx] &= ~mask;
        chunk->live_count--;
    }
    chunk->dirty = true;
}

// 初始化游戏
void init_game(GameState *state, int argc, char** argv) {
    memset(state, 0, sizeof(GameState));
    state->mode = DESIGN;
    state->rows = LINES;
    state->cols = COLS;
    state->cursor_screen_x = state->cols / 2;
    state->cursor_screen_y = state->rows / 2;
    state->prev_cursor_screen_x = -1;
    state->prev_cursor_screen_y = -1;
    state->speed_level = DEFAULT_SPEED_LEVEL;
    state->skip_frames = 0;
    SLIST_INIT(&state->world);
    state->prev_rows = LINES;
    state->prev_cols = COLS;
    
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-z") == 0) {
            state->precompute = true;
            if (i + 1 < argc) {
                char *end;
                long rounds = strtol(argv[i+1], &end, 10);
                if (*end == '\0' && rounds > 0 && rounds <= 1000) {
                    state->precompute_rounds = rounds;
                    i++;
                }
            }
        }
        else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--speed") == 0) {
            if (i + 1 < argc) {
                char *end;
                long speed = strtol(argv[i+1], &end, 10);
                if (*end == '\0' && speed >= MIN_SPEED_LEVEL && speed <= MAX_SPEED_LEVEL) {
                    state->speed_level = speed;
                    i++;
                }
            }
        }
    }
}

// 释放游戏资源
void free_game(GameState *state) {
    free_world(&state->world);
    free(state->dirty_chunks);
    free(state->positions_to_check);
    free(state->updates);
    if (state->command_str) free(state->command_str);
}

// 释放世界资源
void free_world(World *world) {
    RowEntry *row_entry;
    while (!SLIST_EMPTY(world)) {
        row_entry = SLIST_FIRST(world);
        ChunkEntry *chunk_entry;
        while (!SLIST_EMPTY(&row_entry->row)) {
            chunk_entry = SLIST_FIRST(&row_entry->row);
            SLIST_REMOVE_HEAD(&row_entry->row, entries);
            free(chunk_entry->chunk);
            free(chunk_entry);
        }
        SLIST_REMOVE(world, row_entry, RowEntry, entries);
        free(row_entry);
    }
}

// 获取块（如果存在）
Chunk* get_chunk_if_exists(GameState *state, int world_x, int world_y) {
    if (world_x < INT_MIN/2 || world_x > INT_MAX/2 || 
        world_y < INT_MIN/2 || world_y > INT_MAX/2) {
        return NULL;
    }
    
    int chunk_x = world_x / CHUNK_SIZE;
    int chunk_y = world_y / CHUNK_SIZE;
    
    RowEntry *row_entry;
    SLIST_FOREACH(row_entry, &state->world, entries) {
        if (row_entry->key == chunk_y) {
            ChunkEntry *chunk_entry;
            SLIST_FOREACH(chunk_entry, &row_entry->row, entries) {
                if (chunk_entry->key == chunk_x) {
                    return chunk_entry->chunk;
                }
            }
            return NULL;
        }
    }
    return NULL;
}

// 获取或创建块
Chunk* get_chunk(GameState *state, int world_x, int world_y) {
    if (world_x < INT_MIN/2 || world_x > INT_MAX/2 || 
        world_y < INT_MIN/2 || world_y > INT_MAX/2) {
        return NULL;
    }
    
    int chunk_x = world_x / CHUNK_SIZE;
    int chunk_y = world_y / CHUNK_SIZE;
    
    RowEntry *row_entry;
    SLIST_FOREACH(row_entry, &state->world, entries) {
        if (row_entry->key == chunk_y) {
            ChunkEntry *chunk_entry;
            SLIST_FOREACH(chunk_entry, &row_entry->row, entries) {
                if (chunk_entry->key == chunk_x) {
                    return chunk_entry->chunk;
                }
            }
            ChunkEntry *new_entry = malloc(sizeof(ChunkEntry));
            if (!new_entry) return NULL;
            new_entry->key = chunk_x;
            new_entry->chunk = calloc(1, sizeof(Chunk));
            if (!new_entry->chunk) {
                free(new_entry);
                return NULL;
            }
            SLIST_INSERT_HEAD(&row_entry->row, new_entry, entries);
            return new_entry->chunk;
        }
    }
    RowEntry *new_row = malloc(sizeof(RowEntry));
    if (!new_row) return NULL;
    new_row->key = chunk_y;
    SLIST_INIT(&new_row->row);
    
    ChunkEntry *new_entry = malloc(sizeof(ChunkEntry));
    if (!new_entry) {
        free(new_row);
        return NULL;
    }
    new_entry->key = chunk_x;
    new_entry->chunk = calloc(1, sizeof(Chunk));
    if (!new_entry->chunk) {
        free(new_entry);
        free(new_row);
        return NULL;
    }
    SLIST_INSERT_HEAD(&new_row->row, new_entry, entries);
    SLIST_INSERT_HEAD(&state->world, new_row, entries);
    return new_entry->chunk;
}

// 查看细胞状态
bool peek_cell(GameState *state, int world_x, int world_y) {
    if (world_x < INT_MIN + CHUNK_SIZE || world_x > INT_MAX - CHUNK_SIZE ||
        world_y < INT_MIN + CHUNK_SIZE || world_y > INT_MAX - CHUNK_SIZE) {
        return false;
    }
    
    Chunk *chunk = get_chunk_if_exists(state, world_x, world_y);
    if (!chunk) return false;
    
    int local_x = world_x % CHUNK_SIZE;
    int local_y = world_y % CHUNK_SIZE;
    
    if (local_x < 0) local_x += CHUNK_SIZE;
    if (local_y < 0) local_y += CHUNK_SIZE;
    
    return chunk_get_bit(chunk, local_x, local_y);
}

// 设置细胞状态
void set_cell(GameState *state, int world_x, int world_y, bool alive) {
    if (world_x < INT_MIN/2 || world_x > INT_MAX/2 || 
        world_y < INT_MIN/2 || world_y > INT_MAX/2) {
        return;
    }
    
    Chunk *chunk = get_chunk(state, world_x, world_y);
    if (!chunk) return;
    
    int local_x = world_x % CHUNK_SIZE;
    int local_y = world_y % CHUNK_SIZE;
    
    if (local_x < 0) local_x += CHUNK_SIZE;
    if (local_y < 0) local_y += CHUNK_SIZE;
    
    bool current = chunk_get_bit(chunk, local_x, local_y);
    if (current != alive) {
        chunk_set_bit(chunk, local_x, local_y, alive);
        
        if (alive) state->live_cell_count++;
        else state->live_cell_count--;
        
        int chunk_x = world_x / CHUNK_SIZE;
        int chunk_y = world_y / CHUNK_SIZE;
        add_to_dirty_chunks(state, chunk_x, chunk_y);
    }
}

// 比较函数
int pair_compare(const void *a, const void *b) {
    const Pair *pa = (const Pair*)a;
    const Pair *pb = (const Pair*)b;
    if (pa->first != pb->first) return pa->first - pb->first;
    return pa->second - pb->second;
}

// 计算下一代
void compute_generation(GameState *state) {
    if (state->live_cell_count == 0) return;
    
    state->positions_count = 0;
    state->updates_count = 0;
    
    RowEntry *row_entry;
    SLIST_FOREACH(row_entry, &state->world, entries) {
        ChunkEntry *chunk_entry;
        SLIST_FOREACH(chunk_entry, &row_entry->row, entries) {
            Chunk *chunk = chunk_entry->chunk;
            if (!chunk || chunk->live_count == 0) continue;
            
            int world_base_x = chunk_entry->key * CHUNK_SIZE;
            int world_base_y = row_entry->key * CHUNK_SIZE;
            
            for (int y = 0; y < CHUNK_SIZE; y++) {
                for (int x = 0; x < CHUNK_SIZE; x++) {
                    if (!chunk_get_bit(chunk, x, y)) continue;
                    
                    int world_x = world_base_x + x;
                    int world_y = world_base_y + y;
                    
                    for (int i = 0; i < 8; i++) {
                        int nx = world_x + neighbor_offsets[i][0];
                        int ny = world_y + neighbor_offsets[i][1];
                        add_to_positions(state, nx, ny);
                    }
                    add_to_positions(state, world_x, world_y);
                }
            }
        }
    }
    
    if (state->positions_count == 0) return;
    qsort(state->positions_to_check, state->positions_count, sizeof(Pair), pair_compare);
    
    int unique_count = 0;
    for (int i = 0; i < state->positions_count; i++) {
        if (i == 0 || 
            state->positions_to_check[i].first != state->positions_to_check[i-1].first ||
            state->positions_to_check[i].second != state->positions_to_check[i-1].second) {
            state->positions_to_check[unique_count++] = state->positions_to_check[i];
        }
    }
    state->positions_count = unique_count;
    
    for (int i = 0; i < state->positions_count; i++) {
        int world_x = state->positions_to_check[i].first;
        int world_y = state->positions_to_check[i].second;
        int neighbors = 0;
        
        for (int j = 0; j < 8; j++) {
            int nx = world_x + neighbor_offsets[j][0];
            int ny = world_y + neighbor_offsets[j][1];
            if (peek_cell(state, nx, ny)) {
                neighbors++;
            }
        }
        
        bool current = peek_cell(state, world_x, world_y);
        if (current) {
            if (neighbors < 2 || neighbors > 3) {
                add_to_updates(state, world_x, world_y, false);
            }
        } else {
            if (neighbors == 3) {
                add_to_updates(state, world_x, world_y, true);
            }
        }
    }
    
    for (int i = 0; i < state->updates_count; i++) {
        set_cell(state, state->updates[i].x, state->updates[i].y, state->updates[i].alive);
    }
}

// 显示加载进度
void show_loading(GameState *state) {
    clear();
    int width = state->cols - 10;
    if (width > 30) width = 30;
    int start_col = (state->cols - width) / 2;
    int start_row = state->rows / 2;
    
    mvprintw(start_row - 2, start_col, "Precomputing %d rounds...", state->precompute_rounds);
    mvprintw(start_row, start_col - 1, "[");
    mvprintw(start_row, start_col + width, "]");
    refresh();
    
    int refresh_interval = state->precompute_rounds / 50;
    if (refresh_interval < 1) refresh_interval = 1;
    
    for (int i = 0; i < state->precompute_rounds; i++) {
        compute_generation(state);
        
        if (i % refresh_interval == 0) {
            int progress = (i + 1) * width / state->precompute_rounds;
            
            for (int j = 0; j < progress; j++) {
                mvaddch(start_row, start_col + j, '=' | A_REVERSE);
            }
            mvprintw(start_row + 2, start_col, "Progress: %d%%", 
                     (i + 1) * 100 / state->precompute_rounds);
            refresh();
        }
    }
    
    for (int j = 0; j < width; j++) {
        mvaddch(start_row, start_col + j, '=' | A_REVERSE);
    }
    mvprintw(start_row + 2, start_col, "Progress: 100%%");
    refresh();
    
    usleep(200000);
    
    for (int r = start_row - 2; r <= start_row + 2; r++) {
        move(r, 0);
        clrtoeol();
    }
    refresh();
}

// 绘制块
void draw_chunk(GameState *state, int chunk_x, int chunk_y, Chunk *chunk) {
    int world_start_x = chunk_x * CHUNK_SIZE;
    int world_start_y = chunk_y * CHUNK_SIZE;
    
    for (int y = 0; y < CHUNK_SIZE; y++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {
            int screen_x = world_start_x + x - state->viewport_x;
            int screen_y = world_start_y + y - state->viewport_y;
            
            if (screen_x >= 0 && screen_x < state->cols && 
                screen_y >= 0 && screen_y < state->rows) {
                
                if (chunk_get_bit(chunk, x, y)) {
                    mvaddch(screen_y, screen_x, '#' | A_BOLD);
                } else {
                    mvaddch(screen_y, screen_x, ' ');
                }
            }
        }
    }
    
    chunk->dirty = false;
}

// 绘制光标
void draw_cursor(GameState *state) {
    if (state->prev_cursor_screen_y >= 0 && state->prev_cursor_screen_y < state->rows && 
        state->prev_cursor_screen_x >= 0 && state->prev_cursor_screen_x < state->cols) {
        
        int world_x = state->prev_cursor_screen_x + state->viewport_x;
        int world_y = state->prev_cursor_screen_y + state->viewport_y;
        
        bool isAlive = peek_cell(state, world_x, world_y);
        chtype ch = isAlive ? '#' : ' ';
        
        mvaddch(state->prev_cursor_screen_y, state->prev_cursor_screen_x, ch);
    }
    
    state->prev_cursor_screen_x = state->cursor_screen_x;
    state->prev_cursor_screen_y = state->cursor_screen_y;
    
    if (state->cursor_screen_y >= 0 && state->cursor_screen_y < state->rows && 
        state->cursor_screen_x >= 0 && state->cursor_screen_x < state->cols) {
        
        int world_x = state->cursor_screen_x + state->viewport_x;
        int world_y = state->cursor_screen_y + state->viewport_y;
        
        bool isAlive = peek_cell(state, world_x, world_y);
        chtype ch = isAlive ? '#' : ' ';
        
        mvaddch(state->cursor_screen_y, state->cursor_screen_x, ch | A_REVERSE);
    }
}

// 绘制所有可见块
void draw_all_visible_chunks(GameState *state) {
    int min_world_x = state->viewport_x;
    int min_world_y = state->viewport_y;
    int max_world_x = state->viewport_x + state->cols;
    int max_world_y = state->viewport_y + state->rows;
    
    int min_chunk_x = min_world_x / CHUNK_SIZE;
    int min_chunk_y = min_world_y / CHUNK_SIZE;
    int max_chunk_x = (max_world_x + CHUNK_SIZE - 1) / CHUNK_SIZE;
    int max_chunk_y = (max_world_y + CHUNK_SIZE - 1) / CHUNK_SIZE;
    
    for (int chunk_y = min_chunk_y; chunk_y <= max_chunk_y; chunk_y++) {
        RowEntry *row_entry = NULL;
        SLIST_FOREACH(row_entry, &state->world, entries) {
            if (row_entry->key == chunk_y) {
                for (int chunk_x = min_chunk_x; chunk_x <= max_chunk_x; chunk_x++) {
                    ChunkEntry *chunk_entry;
                    SLIST_FOREACH(chunk_entry, &row_entry->row, entries) {
                        if (chunk_entry->key == chunk_x) {
                            draw_chunk(state, chunk_x, chunk_y, chunk_entry->chunk);
                        }
                    }
                }
            }
        }
    }
}

// 保存世界状态
CommandResult save_world(GameState *state, const char* filename) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        return ERROR;
    }

    fprintf(file, "#Life 1.06\n");
    fprintf(file, "# Generated by LifeGame\n");
    fprintf(file, "# Viewport: %d %d\n", state->viewport_x, state->viewport_y);
    fprintf(file, "# Speed: %d\n", state->speed_level);

    RowEntry *row_entry;
    SLIST_FOREACH(row_entry, &state->world, entries) {
        ChunkEntry *chunk_entry;
        SLIST_FOREACH(chunk_entry, &row_entry->row, entries) {
            Chunk *chunk = chunk_entry->chunk;
            if (!chunk || chunk->live_count == 0) continue;
            
            int world_base_x = chunk_entry->key * CHUNK_SIZE;
            int world_base_y = row_entry->key * CHUNK_SIZE;
            
            for (int y = 0; y < CHUNK_SIZE; y++) {
                for (int x = 0; x < CHUNK_SIZE; x++) {
                    if (chunk_get_bit(chunk, x, y)) {
                        fprintf(file, "%d %d\n", world_base_x + x, world_base_y + y);
                    }
                }
            }
        }
    }

    fclose(file);
    return SUCCESS;
}

// 加载世界状态
CommandResult load_world(GameState *state, const char* filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        return ERROR;
    }

    free_world(&state->world);
    SLIST_INIT(&state->world);
    state->live_cell_count = 0;

    char line[256];
    bool viewport_loaded = false;
    bool speed_loaded = false;

    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            if (strstr(line, "# Viewport:") != NULL) {
                if (sscanf(line + 11, "%d %d", &state->viewport_x, &state->viewport_y) == 2) {
                    viewport_loaded = true;
                }
            }
            else if (strstr(line, "# Speed:") != NULL) {
                if (sscanf(line + 8, "%d", &state->speed_level) == 1) {
                    speed_loaded = true;
                }
            }
            continue;
        }

        int x, y;
        if (sscanf(line, "%d %d", &x, &y) == 2) {
            set_cell(state, x, y, true);
        } else {
            fclose(file);
            return ERROR;
        }
    }

    fclose(file);
    
    // 如果没有加载速度，使用默认速度
    if (!speed_loaded) {
        state->speed_level = DEFAULT_SPEED_LEVEL;
    }
    
    if (!viewport_loaded && state->live_cell_count > 0) {
        long long sum_x = 0, sum_y = 0;
        int count = 0;
        
        RowEntry *row_entry;
        SLIST_FOREACH(row_entry, &state->world, entries) {
            ChunkEntry *chunk_entry;
            SLIST_FOREACH(chunk_entry, &row_entry->row, entries) {
                Chunk *chunk = chunk_entry->chunk;
                if (!chunk || chunk->live_count == 0) continue;
                
                int world_base_x = chunk_entry->key * CHUNK_SIZE;
                int world_base_y = row_entry->key * CHUNK_SIZE;
                
                for (int y = 0; y < CHUNK_SIZE; y++) {
                    for (int x = 0; x < CHUNK_SIZE; x++) {
                        if (chunk_get_bit(chunk, x, y)) {
                            sum_x += world_base_x + x;
                            sum_y += world_base_y + y;
                            count++;
                        }
                    }
                }
            }
        }
        
        if (count > 0) {
            state->viewport_x = (int)(sum_x / count) - state->cols / 2;
            state->viewport_y = (int)(sum_y / count) - state->rows / 2;
        }
    }
    
    state->need_full_refresh = true;
    return SUCCESS;
}

// 设计模式
void design_mode(GameState *state) {
    curs_set(1);
    state->dirty_count = 0;
    state->prev_cursor_screen_x = state->cursor_screen_x;
    state->prev_cursor_screen_y = state->cursor_screen_y;
    
    while (state->mode == DESIGN) {
        // 更新终端尺寸
        state->rows = LINES;
        state->cols = COLS;
        
        // 检查终端大小变化
        if (state->rows != state->prev_rows || state->cols != state->prev_cols) {
            state->viewport_changed = true;
            state->need_full_refresh = true;
            // 调整光标位置
            if (state->cursor_screen_x >= state->cols) {
                state->cursor_screen_x = state->cols - 1;
            }
            if (state->cursor_screen_y >= state->rows) {
                state->cursor_screen_y = state->rows - 1;
            }
            state->prev_rows = state->rows;
            state->prev_cols = state->cols;
        }
        
        if (state->viewport_changed || state->need_full_refresh) {
            clear();
            draw_all_visible_chunks(state);
            state->viewport_changed = false;
            state->need_full_refresh = false;
            state->dirty_count = 0;
        } else {
            for (int i = 0; i < state->dirty_count; i++) {
                int chunk_x = state->dirty_chunks[i].first;
                int chunk_y = state->dirty_chunks[i].second;
                Chunk *chunk = get_chunk_if_exists(state, 
                    chunk_x * CHUNK_SIZE, chunk_y * CHUNK_SIZE);
                if (chunk) {
                    draw_chunk(state, chunk_x, chunk_y, chunk);
                }
            }
            state->dirty_count = 0;
        }
        
        draw_cursor(state);
        
        mvprintw(0, 0, "DESIGN MODE - Cells: %d | Cursor: (%d, %d) | Viewport: (%d, %d)", 
                 state->live_cell_count, 
                 state->cursor_screen_x + state->viewport_x, 
                 state->cursor_screen_y + state->viewport_y,
                 state->viewport_x, state->viewport_y);
        clrtoeol();
        refresh();
        
        int ch = getch();
        switch (ch) {
            case 'q': case 'Q':
                state->running = false;
                return;
            case 'c': case 'C':
                state->mode = COMMAND;
                if (state->command_str) free(state->command_str);
                state->command_str = strdup("");
                return;
            case 'y': case 'Y':
                state->mode = PLAY;
                if (state->precompute) {
                    show_loading(state);
                }
                return;
            case 'w': case 'W':
                state->viewport_y--;
                state->viewport_changed = true;
                break;
            case 's': case 'S':
                state->viewport_y++;
                state->viewport_changed = true;
                break;
            case 'a': case 'A':
                state->viewport_x--;
                state->viewport_changed = true;
                break;
            case 'd': case 'D':
                state->viewport_x++;
                state->viewport_changed = true;
                break;
            case KEY_UP:
                if (state->cursor_screen_y > 0) state->cursor_screen_y--;
                break;
            case KEY_DOWN:
                if (state->cursor_screen_y < state->rows - 1) state->cursor_screen_y++;
                break;
            case KEY_LEFT:
                if (state->cursor_screen_x > 0) state->cursor_screen_x--;
                break;
            case KEY_RIGHT:
                if (state->cursor_screen_x < state->cols - 1) state->cursor_screen_x++;
                break;
            case '\n': case ' ':
            {
                int world_x = state->cursor_screen_x + state->viewport_x;
                int world_y = state->cursor_screen_y + state->viewport_y;
                bool current = peek_cell(state, world_x, world_y);
                set_cell(state, world_x, world_y, !current);
                break;
            }
        }
    }
}

// 命令模式
void command_mode(GameState *state) {
    curs_set(1);
    echo();
    
    while (state->mode == COMMAND) {
        // 更新终端尺寸
        state->rows = LINES;
        state->cols = COLS;
        
        // 检查终端大小变化
        if (state->rows != state->prev_rows || state->cols != state->prev_cols) {
            state->prev_rows = state->rows;
            state->prev_cols = state->cols;
        }
        
        move(state->rows - 1, 0);
        clrtoeol();
        printw("CMD:%s", state->command_str);
        move(state->rows - 1, 5 + strlen(state->command_str));
        refresh();
        
        int ch = getch();
        switch (ch) {
            case 27: // ESC
                state->mode = DESIGN;
                break;
            case '\n': // ENTER
            {
                char cmd[32];
                char filename[256] = "pattern.lif";
                if (sscanf(state->command_str, "%31s", cmd) == 1) {
                    if (strcmp(cmd, "save") == 0 || strcmp(cmd, "SAVE") == 0) {
                        sscanf(state->command_str + strlen(cmd), "%255s", filename);
                        
                        CommandResult result = save_world(state, filename);
                        move(state->rows - 2, 0);
                        clrtoeol();
                        if (result == SUCCESS) {
                            printw("Saved to %s", filename);
                        } else {
                            printw("Error saving to %s", filename);
                        }
                        refresh();
                        sleep(1);
                    }
                    else if (strcmp(cmd, "load") == 0 || strcmp(cmd, "LOAD") == 0) {
                        sscanf(state->command_str + strlen(cmd), "%255s", filename);
                        
                        CommandResult result = load_world(state, filename);
                        move(state->rows - 2, 0);
                        clrtoeol();
                        if (result == SUCCESS) {
                            printw("Loaded from %s", filename);
                        } else {
                            printw("Error loading from %s", filename);
                        }
                        refresh();
                        sleep(1);
                    }
                    else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "CLEAR") == 0) {
                        free_world(&state->world);
                        SLIST_INIT(&state->world);
                        state->live_cell_count = 0;
                        state->need_full_refresh = true;
                        move(state->rows - 2, 0);
                        clrtoeol();
                        printw("World cleared");
                        refresh();
                        sleep(1);
                    }
                    else if (strcmp(cmd, "rand") == 0 || strcmp(cmd, "RAND") == 0) {
                        int x, y, w, h;
                        if (sscanf(state->command_str + strlen(cmd), "%d %d %d %d", &x, &y, &w, &h) == 4) {
                            int count = 0;
                            for (int i = y; i < y + h; i++) {
                                for (int j = x; j < x + w; j++) {
                                    if (rand() % 3 == 0) {
                                        set_cell(state, j, i, true);
                                        count++;
                                    }
                                }
                            }
                            state->need_full_refresh = true;
                            
                            move(state->rows - 2, 0);
                            clrtoeol();
                            printw("Generated %d random cells in area [%d, %d] to [%d, %d]", 
                                   count, x, y, x + w - 1, y + h - 1);
                            refresh();
                            sleep(2);
                        } else {
                            move(state->rows - 2, 0);
                            clrtoeol();
                            printw("Usage: rand <x> <y> <width> <height>");
                            refresh();
                            sleep(1);
                        }
                    }
                    else {
                        move(state->rows - 2, 0);
                        clrtoeol();
                        printw("Unknown command: %s", cmd);
                        refresh();
                        sleep(1);
                    }
                }
                state->mode = DESIGN;
                break;
            }
            case 127: case KEY_BACKSPACE:
                if (strlen(state->command_str) > 0) {
                    state->command_str[strlen(state->command_str)-1] = '\0';
                }
                break;
            default:
                if (ch >= 32 && ch <= 126) {
                    char temp[2] = {ch, '\0'};
                    char *new_str = malloc(strlen(state->command_str) + 2);
                    if (new_str) {
                        strcpy(new_str, state->command_str);
                        strcat(new_str, temp);
                        free(state->command_str);
                        state->command_str = new_str;
                    }
                }
                break;
        }
    }
    
    noecho();
}

// 播放模式
void play_mode(GameState *state) {
    curs_set(0);
    nodelay(stdscr, TRUE);
    state->dirty_count = 0;
    
    int generation_count = 0;
    uint64_t last_frame_time = get_time_ms();
    
    while (state->mode == PLAY) {
        // 更新终端尺寸
        state->rows = LINES;
        state->cols = COLS;
        
        // 检查终端大小变化
        if (state->rows != state->prev_rows || state->cols != state->prev_cols) {
            state->viewport_changed = true;
            state->need_full_refresh = true;
            state->prev_rows = state->rows;
            state->prev_cols = state->cols;
        }
        
        // 计算当前帧时间
        uint64_t current_time = get_time_ms();
        uint64_t frame_time = current_time - last_frame_time;
        
        // 计算目标帧时间（基于速度等级）
        int target_frame_time = 1000 / (state->speed_level * 2);
        if (target_frame_time < 10) target_frame_time = 10; // 最小10ms
        
        // 如果还没到下一帧时间，等待
        if (frame_time < target_frame_time) {
            usleep((target_frame_time - frame_time) * 1000);
            continue;
        }
        
        last_frame_time = current_time;
        
        // 计算下一代
        if (state->live_cell_count > 0) {
            compute_generation(state);
            generation_count++;
        }
        
        // 绘制
        if (state->viewport_changed || state->need_full_refresh) {
            clear();
            draw_all_visible_chunks(state);
            state->viewport_changed = false;
            state->need_full_refresh = false;
            state->dirty_count = 0;
        } else {
            for (int i = 0; i < state->dirty_count; i++) {
                int chunk_x = state->dirty_chunks[i].first;
                int chunk_y = state->dirty_chunks[i].second;
                Chunk *chunk = get_chunk_if_exists(state, 
                    chunk_x * CHUNK_SIZE, chunk_y * CHUNK_SIZE);
                if (chunk) {
                    draw_chunk(state, chunk_x, chunk_y, chunk);
                }
            }
            state->dirty_count = 0;
        }
        
        // 显示游戏状态信息
        mvprintw(0, 0, "PLAY MODE - Gen: %d, Cells: %d | Speed: %d/%d [%c%c]", 
                 generation_count, state->live_cell_count,
                 state->speed_level, MAX_SPEED_LEVEL,
                 state->speed_level > MIN_SPEED_LEVEL ? '-' : ' ',
                 state->speed_level < MAX_SPEED_LEVEL ? '+' : ' ');
        clrtoeol();
        refresh();
        
        // 处理输入
        int ch = getch();
        if (ch == 'q' || ch == 'Q') {
            state->mode = DESIGN;
        } else if (ch == 'w' || ch == 'W') {
            state->viewport_y--;
            state->viewport_changed = true;
        } else if (ch == 's' || ch == 'S') {
            state->viewport_y++;
            state->viewport_changed = true;
        } else if (ch == 'a' || ch == 'A') {
            state->viewport_x--;
            state->viewport_changed = true;
        } else if (ch == 'd' || ch == 'D') {
            state->viewport_x++;
            state->viewport_changed = true;
        } else if (ch == '+' || ch == '=') {
            adjust_speed(state, 1); // 加快速度
        } else if (ch == '-' || ch == '_') {
            adjust_speed(state, -1); // 减慢速度
        }
    }
    
    nodelay(stdscr, FALSE);
}

// 主函数
int main(int argc, char** argv) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(0);
    set_escdelay(25);
    start_color();
    use_default_colors();
    
    srand(time(NULL));
    
    GameState state;
    init_game(&state, argc, argv);
    state.running = true;
    
    while (state.running) {
        switch (state.mode) {
            case DESIGN: design_mode(&state); break;
            case COMMAND: command_mode(&state); break;
            case PLAY: play_mode(&state); break;
        }
    }
    
    free_game(&state);
    endwin();
    return 0;
}