// g++ -o LifeGame LifeGame.cpp -lncurses -pthread  // 通用编译
// g++ -m32 -O3 -DARM_OPTIMIZED -o LifeGame32 LifeGame.cpp -lncurses -pthread  // 32位优化
// g++ -O3 -DX64_OPTIMIZED -o LifeGame64 LifeGame.cpp -lncurses -pthread    // 64位优化

#include <ncurses.h>
#include <vector>
#include <string>
#include <cstdlib>
#include <ctime>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstring>
#include <utility>
#include <cstdint>
#include <climits>
#include <fstream>
#include <sstream>
#include <iomanip>
using namespace std;

// 架构特定配置
#ifdef ARM_OPTIMIZED
    // 32位ARM优化配置
    const int CHUNK_SIZE = 32;
    using BitmapType = uint32_t;
    const int BITS_PER_UNIT = 32;
    const int BITMAP_SIZE = (CHUNK_SIZE * CHUNK_SIZE + BITS_PER_UNIT - 1) / BITS_PER_UNIT;
#elif defined(X64_OPTIMIZED)
    // 64位x86优化配置
    const int CHUNK_SIZE = 64;
    using BitmapType = uint64_t;
    const int BITS_PER_UNIT = 64;
    const int BITMAP_SIZE = (CHUNK_SIZE * CHUNK_SIZE + BITS_PER_UNIT - 1) / BITS_PER_UNIT;
#else
    // 通用配置
    const int CHUNK_SIZE = 32;
    using BitmapType = uint32_t;
    const int BITS_PER_UNIT = 32;
    const int BITMAP_SIZE = (CHUNK_SIZE * CHUNK_SIZE + BITS_PER_UNIT - 1) / BITS_PER_UNIT;
#endif

// 优化邻居计算
const int neighbor_offsets[8][2] = {
    {-1, -1}, {0, -1}, {1, -1},
    {-1,  0},           {1,  0},
    {-1,  1}, {0,  1}, {1,  1}
};

struct Chunk {
    BitmapType bitmap[BITMAP_SIZE] = {0}; // 位图存储
    bool dirty = true;
    int live_count = 0; // 当前块的活细胞计数

    // 获取位值（架构优化版本）
    inline bool get_bit(int x, int y) const {
        // 使用int64_t防止溢出
        int64_t pos = static_cast<int64_t>(y) * CHUNK_SIZE + x;
        if (pos < 0 || pos >= CHUNK_SIZE * CHUNK_SIZE) return false;
        size_t idx = static_cast<size_t>(pos) / BITS_PER_UNIT;
        return (bitmap[idx] >> (pos % BITS_PER_UNIT)) & 1;
    }
    
    // 设置位值（架构优化版本）
    inline void set_bit(int x, int y, bool value) {
        // 使用int64_t防止溢出
        int64_t pos = static_cast<int64_t>(y) * CHUNK_SIZE + x;
        if (pos < 0 || pos >= CHUNK_SIZE * CHUNK_SIZE) return;
        size_t idx = static_cast<size_t>(pos) / BITS_PER_UNIT;
        BitmapType mask = static_cast<BitmapType>(1) << (pos % BITS_PER_UNIT);
        
        bool current = (bitmap[idx] & mask) != 0;
        if (current == value) return;
        
        if (value) {
            bitmap[idx] |= mask;
            live_count++;
        } else {
            bitmap[idx] &= ~mask;
            live_count--;
        }
        dirty = true;
    }
};

enum Mode { DESIGN, COMMAND, PLAY };

struct GameState {
    Mode mode = DESIGN;
    int rows, cols;
    
    // 使用块存储的世界
    unordered_map<int, unordered_map<int, Chunk*>> world;
    
    int live_cell_count = 0;
    int viewport_x = 0;
    int viewport_y = 0;
    int cursor_screen_x = 0;
    int cursor_screen_y = 0;
    int prev_cursor_screen_x = -1;
    int prev_cursor_screen_y = -1;
    bool viewport_changed = false;
    bool need_full_refresh = false;
    
    string command_str;
    bool precompute = false;
    int precompute_rounds = 20;
    bool running = true;
    
    vector<pair<int, int>> dirty_chunks;
    
    // 重用数据结构减少内存分配
    vector<pair<int, int>> positions_to_check;
    vector<tuple<int, int, bool>> updates;
    
    // 析构函数释放内存
    ~GameState() {
        for (auto& [y, row] : world) {
            for (auto& [x, chunk] : row) {
                delete chunk;
            }
        }
    }
};

Chunk* get_chunk_if_exists(GameState& state, int world_x, int world_y) {
    // 防止极端坐标值
    if (world_x < INT_MIN/2 || world_x > INT_MAX/2 || 
        world_y < INT_MIN/2 || world_y > INT_MAX/2) {
        return nullptr;
    }
    
    int chunk_x = world_x / CHUNK_SIZE;
    int chunk_y = world_y / CHUNK_SIZE;
    
    auto row_it = state.world.find(chunk_y);
    if (row_it == state.world.end()) return nullptr;
    
    auto chunk_it = row_it->second.find(chunk_x);
    return (chunk_it != row_it->second.end()) ? chunk_it->second : nullptr;
}

Chunk* get_chunk(GameState& state, int world_x, int world_y) {
    // 防止极端坐标值
    if (world_x < INT_MIN/2 || world_x > INT_MAX/2 || 
        world_y < INT_MIN/2 || world_y > INT_MAX/2) {
        return nullptr;
    }
    
    int chunk_x = world_x / CHUNK_SIZE;
    int chunk_y = world_y / CHUNK_SIZE;
    
    auto& row = state.world[chunk_y];
    auto it = row.find(chunk_x);
    if (it != row.end()) return it->second;
    
    Chunk* chunk = new Chunk();
    row[chunk_x] = chunk;
    return chunk;
}

bool peek_cell(GameState& state, int world_x, int world_y) {
    // 显式检查坐标是否在有效范围内
    if (world_x < INT_MIN + CHUNK_SIZE || world_x > INT_MAX - CHUNK_SIZE ||
        world_y < INT_MIN + CHUNK_SIZE || world_y > INT_MAX - CHUNK_SIZE) {
        return false;
    }
    
    Chunk* chunk = get_chunk_if_exists(state, world_x, world_y);
    if (!chunk) return false;
    
    int local_x = world_x % CHUNK_SIZE;
    int local_y = world_y % CHUNK_SIZE;
    
    // 处理负数坐标的取模
    if (local_x < 0) local_x += CHUNK_SIZE;
    if (local_y < 0) local_y += CHUNK_SIZE;
    
    return chunk->get_bit(local_x, local_y);
}

void set_cell(GameState& state, int world_x, int world_y, bool alive) {
    // 防止极端坐标值
    if (world_x < INT_MIN/2 || world_x > INT_MAX/2 || 
        world_y < INT_MIN/2 || world_y > INT_MAX/2) {
        return;
    }
    
    Chunk* chunk = get_chunk(state, world_x, world_y);
    int local_x = world_x % CHUNK_SIZE;
    int local_y = world_y % CHUNK_SIZE;
    
    // 处理负数坐标的取模
    if (local_x < 0) local_x += CHUNK_SIZE;
    if (local_y < 0) local_y += CHUNK_SIZE;
    
    bool current = chunk->get_bit(local_x, local_y);
    if (current != alive) {
        chunk->set_bit(local_x, local_y, alive);
        
        if (alive) state.live_cell_count++;
        else state.live_cell_count--;
        
        int chunk_x = world_x / CHUNK_SIZE;
        int chunk_y = world_y / CHUNK_SIZE;
        state.dirty_chunks.push_back({chunk_x, chunk_y});
    }
}

void init_game(GameState &state, int argc, char** argv) {
    state.rows = LINES;
    state.cols = COLS;
    state.cursor_screen_x = state.cols / 2;
    state.cursor_screen_y = state.rows / 2;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-z") == 0) {
            state.precompute = true;
            if (i + 1 < argc) {
                char* end;
                long rounds = strtol(argv[i+1], &end, 10);
                if (*end == '\0' && rounds > 0 && rounds <= 1000) {
                    state.precompute_rounds = rounds;
                    i++;
                }
            }
        }
    }
    
    // 预分配内存
    state.positions_to_check.reserve(2048);
    state.updates.reserve(1024);
}

void compute_generation(GameState &state) {
    if (state.live_cell_count == 0) return;
    
    state.positions_to_check.clear();
    state.updates.clear();
    
    // 只处理包含活细胞的块
    for (auto& [chunk_y, row] : state.world) {
        for (auto& [chunk_x, chunk] : row) {
            if (!chunk || chunk->live_count == 0) continue;
            
            int world_base_x = chunk_x * CHUNK_SIZE;
            int world_base_y = chunk_y * CHUNK_SIZE;
            
            // 扫描位图寻找活细胞
            for (int y = 0; y < CHUNK_SIZE; y++) {
                for (int x = 0; x < CHUNK_SIZE; x++) {
                    if (!chunk->get_bit(x, y)) continue;
                    
                    int world_x = world_base_x + x;
                    int world_y = world_base_y + y;
                    
                    // 添加活细胞及其邻居
                    for (int i = 0; i < 8; i++) {
                        int nx = world_x + neighbor_offsets[i][0];
                        int ny = world_y + neighbor_offsets[i][1];
                        state.positions_to_check.emplace_back(nx, ny);
                    }
                    state.positions_to_check.emplace_back(world_x, world_y);
                }
            }
        }
    }
    
    // 排序去重
    sort(state.positions_to_check.begin(), state.positions_to_check.end());
    auto last = unique(state.positions_to_check.begin(), state.positions_to_check.end());
    state.positions_to_check.erase(last, state.positions_to_check.end());
    
    // 计算每个位置的下一代状态
    for (auto& pos : state.positions_to_check) {
        int world_x = pos.first;
        int world_y = pos.second;
        int neighbors = 0;
        
        // 计算邻居
        for (int i = 0; i < 8; i++) {
            int nx = world_x + neighbor_offsets[i][0];
            int ny = world_y + neighbor_offsets[i][1];
            
            if (peek_cell(state, nx, ny)) {
                neighbors++;
            }
        }
        
        bool current = peek_cell(state, world_x, world_y);
        if (current) {
            if (neighbors < 2 || neighbors > 3) {
                state.updates.emplace_back(world_x, world_y, false);
            }
        } else {
            if (neighbors == 3) {
                state.updates.emplace_back(world_x, world_y, true);
            }
        }
    }
    
    // 应用更新
    for (auto& update : state.updates) {
        set_cell(state, get<0>(update), get<1>(update), get<2>(update));
    }
}

void show_loading(GameState &state) {
    clear();
    int width = min(30, state.cols - 10);
    int start_col = (state.cols - width) / 2;
    int start_row = state.rows / 2;
    
    mvprintw(start_row - 2, start_col, "Precomputing %d rounds...", state.precompute_rounds);
    mvprintw(start_row, start_col - 1, "[");
    mvprintw(start_row, start_col + width, "]");
    refresh();
    
    int refresh_interval = max(1, state.precompute_rounds / 50);
    
    for (int i = 0; i < state.precompute_rounds; i++) {
        compute_generation(state);
        
        if (i % refresh_interval == 0) {
            int progress = (i + 1) * width / state.precompute_rounds;
            
            for (int j = 0; j < progress; j++) {
                mvaddch(start_row, start_col + j, '=' | A_REVERSE);
            }
            mvprintw(start_row + 2, start_col, "Progress: %d%%", 
                     (i + 1) * 100 / state.precompute_rounds);
            refresh();
        }
    }
    
    for (int j = 0; j < width; j++) {
        mvaddch(start_row, start_col + j, '=' | A_REVERSE);
    }
    mvprintw(start_row + 2, start_col, "Progress: 100%%");
    refresh();
    
    this_thread::sleep_for(chrono::milliseconds(200));
    
    for (int r = start_row - 2; r <= start_row + 2; r++) {
        move(r, 0);
        clrtoeol();
    }
    refresh();
}

void draw_chunk(GameState& state, int chunk_x, int chunk_y, Chunk* chunk) {
    int world_start_x = chunk_x * CHUNK_SIZE;
    int world_start_y = chunk_y * CHUNK_SIZE;
    
    for (int y = 0; y < CHUNK_SIZE; y++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {
            int screen_x = world_start_x + x - state.viewport_x;
            int screen_y = world_start_y + y - state.viewport_y;
            
            if (screen_x >= 0 && screen_x < state.cols && 
                screen_y >= 0 && screen_y < state.rows) {
                
                if (chunk->get_bit(x, y)) {
                    mvaddch(screen_y, screen_x, '#' | A_BOLD);
                } else {
                    mvaddch(screen_y, screen_x, ' ');
                }
            }
        }
    }
    
    chunk->dirty = false;
}

void draw_cursor(GameState &state) {
    if (state.prev_cursor_screen_y >= 0 && state.prev_cursor_screen_y < state.rows && 
        state.prev_cursor_screen_x >= 0 && state.prev_cursor_screen_x < state.cols) {
        
        int world_x = state.prev_cursor_screen_x + state.viewport_x;
        int world_y = state.prev_cursor_screen_y + state.viewport_y;
        
        bool isAlive = peek_cell(state, world_x, world_y);
        chtype ch = isAlive ? '#' : ' ';
        
        mvaddch(state.prev_cursor_screen_y, state.prev_cursor_screen_x, ch);
    }
    
    state.prev_cursor_screen_x = state.cursor_screen_x;
    state.prev_cursor_screen_y = state.cursor_screen_y;
    
    if (state.cursor_screen_y >= 0 && state.cursor_screen_y < state.rows && 
        state.cursor_screen_x >= 0 && state.cursor_screen_x < state.cols) {
        
        int world_x = state.cursor_screen_x + state.viewport_x;
        int world_y = state.cursor_screen_y + state.viewport_y;
        
        bool isAlive = peek_cell(state, world_x, world_y);
        chtype ch = isAlive ? '#' : ' ';
        
        mvaddch(state.cursor_screen_y, state.cursor_screen_x, ch | A_REVERSE);
    }
}

void draw_all_visible_chunks(GameState &state) {
    int min_world_x = state.viewport_x;
    int min_world_y = state.viewport_y;
    int max_world_x = state.viewport_x + state.cols;
    int max_world_y = state.viewport_y + state.rows;
    
    int min_chunk_x = min_world_x / CHUNK_SIZE;
    int min_chunk_y = min_world_y / CHUNK_SIZE;
    int max_chunk_x = (max_world_x + CHUNK_SIZE - 1) / CHUNK_SIZE;
    int max_chunk_y = (max_world_y + CHUNK_SIZE - 1) / CHUNK_SIZE;
    
    for (int chunk_y = min_chunk_y; chunk_y <= max_chunk_y; chunk_y++) {
        if (state.world.find(chunk_y) == state.world.end()) continue;
        
        for (int chunk_x = min_chunk_x; chunk_x <= max_chunk_x; chunk_x++) {
            if (state.world[chunk_y].find(chunk_x) == state.world[chunk_y].end()) continue;
            
            draw_chunk(state, chunk_x, chunk_y, state.world[chunk_y][chunk_x]);
        }
    }
}

enum CommandResult { SUCCESS, ERROR, CANCEL };

CommandResult save_world(GameState &state, const string& filename) {
    ofstream file(filename);
    if (!file.is_open()) {
        return ERROR;
    }

    // 写入元数据
    file << "#Life 1.06\n";
    file << "# Generated by LifeGame\n";
    file << "# Viewport: " << state.viewport_x << " " << state.viewport_y << "\n";

    // 写入所有活细胞坐标
    for (const auto& [chunk_y, row] : state.world) {
        for (const auto& [chunk_x, chunk] : row) {
            if (!chunk || chunk->live_count == 0) continue;
            
            int world_base_x = chunk_x * CHUNK_SIZE;
            int world_base_y = chunk_y * CHUNK_SIZE;
            
            for (int y = 0; y < CHUNK_SIZE; y++) {
                for (int x = 0; x < CHUNK_SIZE; x++) {
                    if (chunk->get_bit(x, y)) {
                        file << world_base_x + x << " " << world_base_y + y << "\n";
                    }
                }
            }
        }
    }

    file.close();
    return SUCCESS;
}

CommandResult load_world(GameState &state, const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        return ERROR;
    }

    // 清除当前世界
    for (auto& [y, row] : state.world) {
        for (auto& [x, chunk] : row) {
            delete chunk;
        }
    }
    state.world.clear();
    state.live_cell_count = 0;

    string line;
    int line_num = 0;
    bool viewport_loaded = false;

    while (getline(file, line)) {
        line_num++;
        // 跳过注释行
        if (line.empty() || line[0] == '#') {
            // 尝试解析视口信息
            if (line.find("# Viewport:") != string::npos) {
                istringstream iss(line.substr(11));
                if (iss >> state.viewport_x >> state.viewport_y) {
                    viewport_loaded = true;
                }
            }
            continue;
        }

        // 解析坐标
        istringstream iss(line);
        int x, y;
        if (!(iss >> x >> y)) {
            // 格式错误
            file.close();
            return ERROR;
        }

        set_cell(state, x, y, true);
    }

    file.close();
    
    // 如果文件中没有视口信息，将视口中心设置为活细胞的中心
    if (!viewport_loaded && state.live_cell_count > 0) {
        long long sum_x = 0, sum_y = 0;
        int count = 0;
        
        for (const auto& [chunk_y, row] : state.world) {
            for (const auto& [chunk_x, chunk] : row) {
                if (!chunk || chunk->live_count == 0) continue;
                
                int world_base_x = chunk_x * CHUNK_SIZE;
                int world_base_y = chunk_y * CHUNK_SIZE;
                
                for (int y = 0; y < CHUNK_SIZE; y++) {
                    for (int x = 0; x < CHUNK_SIZE; x++) {
                        if (chunk->get_bit(x, y)) {
                            sum_x += world_base_x + x;
                            sum_y += world_base_y + y;
                            count++;
                        }
                    }
                }
            }
        }
        
        if (count > 0) {
            state.viewport_x = static_cast<int>(sum_x / count) - state.cols / 2;
            state.viewport_y = static_cast<int>(sum_y / count) - state.rows / 2;
        }
    }
    
    state.need_full_refresh = true;
    return SUCCESS;
}

void design_mode(GameState &state) {
    curs_set(1);
    state.dirty_chunks.clear();
    
    state.prev_cursor_screen_x = state.cursor_screen_x;
    state.prev_cursor_screen_y = state.cursor_screen_y;
    
    while (state.mode == DESIGN) {
        if (state.viewport_changed || state.need_full_refresh) {
            clear();
            draw_all_visible_chunks(state);
            state.viewport_changed = false;
            state.need_full_refresh = false;
            state.dirty_chunks.clear();
        } else {
            for (auto& chunk_pos : state.dirty_chunks) {
                int chunk_x = chunk_pos.first;
                int chunk_y = chunk_pos.second;
                if (state.world.find(chunk_y) != state.world.end() && 
                    state.world[chunk_y].find(chunk_x) != state.world[chunk_y].end()) {
                    draw_chunk(state, chunk_x, chunk_y, state.world[chunk_y][chunk_x]);
                }
            }
            state.dirty_chunks.clear();
        }
        
        draw_cursor(state);
        
        mvprintw(0, 0, "DESIGN MODE - Cells: %d | Cursor: (%d, %d) | Viewport: (%d, %d)", 
                 state.live_cell_count, 
                 state.cursor_screen_x + state.viewport_x, 
                 state.cursor_screen_y + state.viewport_y,
                 state.viewport_x, state.viewport_y);
        clrtoeol();
        refresh();
        
        int ch = getch();
        switch (ch) {
            case 'q': case 'Q':
                state.running = false;
                return;
            case 'c': case 'C':
                state.mode = COMMAND;
                state.command_str.clear();
                return;
            case 'y': case 'Y':
                state.mode = PLAY;
                if (state.precompute) {
                    show_loading(state);
                }
                return;
            case 'w': case 'W':
                state.viewport_y--;
                state.viewport_changed = true;
                break;
            case 's': case 'S':
                state.viewport_y++;
                state.viewport_changed = true;
                break;
            case 'a': case 'A':
                state.viewport_x--;
                state.viewport_changed = true;
                break;
            case 'd': case 'D':
                state.viewport_x++;
                state.viewport_changed = true;
                break;
            case KEY_UP:
                if (state.cursor_screen_y > 0) state.cursor_screen_y--;
                break;
            case KEY_DOWN:
                if (state.cursor_screen_y < state.rows - 1) state.cursor_screen_y++;
                break;
            case KEY_LEFT:
                if (state.cursor_screen_x > 0) state.cursor_screen_x--;
                break;
            case KEY_RIGHT:
                if (state.cursor_screen_x < state.cols - 1) state.cursor_screen_x++;
                break;
            case '\n': case ' ':
            {
                int world_x = state.cursor_screen_x + state.viewport_x;
                int world_y = state.cursor_screen_y + state.viewport_y;
                bool current = peek_cell(state, world_x, world_y);
                set_cell(state, world_x, world_y, !current);
                break;
            }
        }
    }
}

void command_mode(GameState &state) {
    curs_set(1);
    echo();
    
    while (state.mode == COMMAND) {
        move(state.rows - 1, 0);
        clrtoeol();
        printw("CMD:%s", state.command_str.c_str());
        move(state.rows - 1, 5 + state.command_str.size());
        refresh();
        
        int ch = getch();
        switch (ch) {
            case 27: // ESC
                state.mode = DESIGN;
                break;
            case '\n': // ENTER
            {
                istringstream iss(state.command_str);
                string cmd;
                iss >> cmd;
                
                if (cmd == "save" || cmd == "SAVE") {
                    string filename;
                    iss >> filename;
                    if (filename.empty()) {
                        filename = "pattern.lif";
                    }
                    
                    auto result = save_world(state, filename);
                    move(state.rows - 2, 0);
                    clrtoeol();
                    if (result == SUCCESS) {
                        printw("Saved to %s", filename.c_str());
                    } else {
                        printw("Error saving to %s", filename.c_str());
                    }
                    refresh();
                    this_thread::sleep_for(chrono::seconds(1));
                }
                else if (cmd == "load" || cmd == "LOAD") {
                    string filename;
                    iss >> filename;
                    if (filename.empty()) {
                        filename = "pattern.lif";
                    }
                    
                    auto result = load_world(state, filename);
                    move(state.rows - 2, 0);
                    clrtoeol();
                    if (result == SUCCESS) {
                        printw("Loaded from %s", filename.c_str());
                    } else {
                        printw("Error loading from %s", filename.c_str());
                    }
                    refresh();
                    this_thread::sleep_for(chrono::seconds(1));
                }
                else if (cmd == "clear" || cmd == "CLEAR") {
                    // 清空世界
                    for (auto& [y, row] : state.world) {
                        for (auto& [x, chunk] : row) {
                            delete chunk;
                        }
                    }
                    state.world.clear();
                    state.live_cell_count = 0;
                    state.need_full_refresh = true;
                    move(state.rows - 2, 0);
                    clrtoeol();
                    printw("World cleared");
                    refresh();
                    this_thread::sleep_for(chrono::seconds(1));
                }
                else if (cmd == "rand" || cmd == "RAND") {
                    int x, y, w, h;
                    if (iss >> x >> y >> w >> h) {
                        int count = 0;
                        for (int i = y; i < y + h; i++) {
                            for (int j = x; j < x + w; j++) {
                                if (rand() % 3 == 0) {
                                    set_cell(state, j, i, true);
                                    count++;
                                }
                            }
                        }
                        state.need_full_refresh = true;
                        
                        // 显示生成结果
                        move(state.rows - 2, 0);
                        clrtoeol();
                        printw("Generated %d random cells in area [%d, %d] to [%d, %d]", 
                               count, x, y, x + w - 1, y + h - 1);
                        refresh();
                        this_thread::sleep_for(chrono::seconds(2));
                    } else {
                        // 参数错误提示
                        move(state.rows - 2, 0);
                        clrtoeol();
                        printw("Usage: rand <x> <y> <width> <height>");
                        refresh();
                        this_thread::sleep_for(chrono::seconds(1));
                    }
                }
                else {
                    // 未知命令提示
                    move(state.rows - 2, 0);
                    clrtoeol();
                    printw("Unknown command: %s", cmd.c_str());
                    refresh();
                    this_thread::sleep_for(chrono::seconds(1));
                }
                
                state.mode = DESIGN;
                break;
            }
            case 127: case KEY_BACKSPACE:
                if (!state.command_str.empty()) {
                    state.command_str.pop_back();
                }
                break;
            default:
                if (ch >= 32 && ch <= 126) {
                    state.command_str += ch;
                }
                break;
        }
    }
    
    noecho();
}

void play_mode(GameState &state) {
    curs_set(0);
    nodelay(stdscr, TRUE);
    state.dirty_chunks.clear();
    
    int generation_count = 0;
    int frames_skipped = 0;
    const int max_skip_frames = 3;
    
    while (state.mode == PLAY) {
        auto start_time = chrono::steady_clock::now();
        
        if (state.live_cell_count > 0) {
            compute_generation(state);
            generation_count++;
        }
        
        auto compute_time = chrono::steady_clock::now();
        
        bool should_draw = true;
        
        if (frames_skipped < max_skip_frames && 
            chrono::duration_cast<chrono::milliseconds>(compute_time - start_time).count() > 80) {
            should_draw = false;
            frames_skipped++;
        } else {
            frames_skipped = 0;
        }
        
        if (should_draw) {
            if (state.viewport_changed || state.need_full_refresh) {
                clear();
                draw_all_visible_chunks(state);
                state.viewport_changed = false;
                state.need_full_refresh = false;
                state.dirty_chunks.clear();
            } else {
                for (auto& chunk_pos : state.dirty_chunks) {
                    int chunk_x = chunk_pos.first;
                    int chunk_y = chunk_pos.second;
                    if (state.world.find(chunk_y) != state.world.end() && 
                        state.world[chunk_y].find(chunk_x) != state.world[chunk_y].end()) {
                        draw_chunk(state, chunk_x, chunk_y, state.world[chunk_y][chunk_x]);
                    }
                }
                state.dirty_chunks.clear();
            }
            
            auto draw_time = chrono::steady_clock::now();
            auto compute_duration = chrono::duration_cast<chrono::milliseconds>(compute_time - start_time);
            auto draw_duration = chrono::duration_cast<chrono::milliseconds>(draw_time - compute_time);
            
            mvprintw(0, 0, "PLAY MODE - Gen: %d, Cells: %d | Compute: %lldms | Draw: %lldms", 
                     generation_count, state.live_cell_count, compute_duration.count(), draw_duration.count());
            clrtoeol();
            refresh();
        }
        
        int ch = getch();
        if (ch == 'q' || ch == 'Q') {
            state.mode = DESIGN;
        } else if (ch == 'w' || ch == 'W') {
            state.viewport_y--;
            state.viewport_changed = true;
        } else if (ch == 's' || ch == 'S') {
            state.viewport_y++;
            state.viewport_changed = true;
        } else if (ch == 'a' || ch == 'A') {
            state.viewport_x--;
            state.viewport_changed = true;
        } else if (ch == 'd' || ch == 'D') {
            state.viewport_x++;
            state.viewport_changed = true;
        }
        
        auto frame_time = chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - start_time).count();
        
        int delay = max(0, 100 - (int)frame_time);
        this_thread::sleep_for(chrono::milliseconds(delay));
    }
    
    nodelay(stdscr, FALSE);
}

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
    
    srand(time(nullptr));
    
    GameState state;
    init_game(state, argc, argv);
    
    while (state.running) {
        switch (state.mode) {
            case DESIGN: design_mode(state); break;
            case COMMAND: command_mode(state); break;
            case PLAY: play_mode(state); break;
        }
    }
    
    endwin();
    return 0;
}