// g++ -o LifeGame LifeGame.cpp -lncurses -pthread
// ./LifeGame  =>  普通模式
// ./LifeGame -z <预演算次数>  =>  在展示演算结果之前先演算100轮再展示

#include <ncurses.h>
#include <vector>
#include <string>
#include <cstdlib>
#include <ctime>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstring>
using namespace std;

// 使用稀疏矩阵存储活细胞，并优化邻居计算
const int CHUNK_SIZE = 16; // 使用16x16的块来组织细胞
struct Chunk {
    bool cells[CHUNK_SIZE][CHUNK_SIZE] = {{false}};
    bool dirty = true; // 标记是否需要重绘
};

enum Mode { DESIGN, COMMAND, PLAY };

struct GameState {
    Mode mode = DESIGN;
    int rows, cols;
    
    // 使用块存储的世界（稀疏矩阵）
    unordered_map<int, unordered_map<int, Chunk*>> world;
    
    // 活动细胞计数器（用于性能优化）
    int live_cell_count = 0;
    
    // 视口位置（左上角的世界坐标）
    int viewport_x = 0;
    int viewport_y = 0;
    
    // 光标位置（屏幕坐标）
    int cursor_screen_x = 0;
    int cursor_screen_y = 0;
    
    // 上一次光标位置（用于修复光标痕迹）
    int prev_cursor_screen_x = -1;
    int prev_cursor_screen_y = -1;
    
    // 标记视口是否改变（需要重绘所有可见块）
    bool viewport_changed = false;
    
    // 标记需要完全刷新视口
    bool need_full_refresh = false;
    
    string command_str;
    bool precompute = false;
    int precompute_rounds = 20; // 默认预计算轮数
    bool running = true;
    
    // 脏矩形集合（优化绘制）
    vector<pair<int, int>> dirty_chunks;
};

// 获取指定坐标的块（如果不存在则返回nullptr）
Chunk* get_chunk_if_exists(GameState& state, int world_x, int world_y) {
    int chunk_x = world_x / CHUNK_SIZE;
    int chunk_y = world_y / CHUNK_SIZE;
    
    if (state.world.find(chunk_y) == state.world.end()) {
        return nullptr;
    }
    
    if (state.world[chunk_y].find(chunk_x) == state.world[chunk_y].end()) {
        return nullptr;
    }
    
    return state.world[chunk_y][chunk_x];
}

// 获取指定坐标的块（如果不存在则创建）
Chunk* get_chunk(GameState& state, int world_x, int world_y) {
    int chunk_x = world_x / CHUNK_SIZE;
    int chunk_y = world_y / CHUNK_SIZE;
    
    if (state.world.find(chunk_y) == state.world.end()) {
        state.world[chunk_y] = unordered_map<int, Chunk*>();
    }
    
    if (state.world[chunk_y].find(chunk_x) == state.world[chunk_y].end()) {
        state.world[chunk_y][chunk_x] = new Chunk();
    }
    
    return state.world[chunk_y][chunk_x];
}

// 获取细胞状态（不创建新块）
bool peek_cell(GameState& state, int world_x, int world_y) {
    Chunk* chunk = get_chunk_if_exists(state, world_x, world_y);
    if (chunk == nullptr) {
        return false; // 块不存在，细胞为死状态
    }
    
    int local_x = world_x % CHUNK_SIZE;
    int local_y = world_y % CHUNK_SIZE;
    return chunk->cells[local_y][local_x];
}

// 设置细胞状态
void set_cell(GameState& state, int world_x, int world_y, bool alive) {
    Chunk* chunk = get_chunk(state, world_x, world_y);
    int local_x = world_x % CHUNK_SIZE;
    int local_y = world_y % CHUNK_SIZE;
    
    bool current = chunk->cells[local_y][local_x];
    if (current != alive) {
        chunk->cells[local_y][local_x] = alive;
        chunk->dirty = true;
        
        // 更新活细胞计数
        if (alive) state.live_cell_count++;
        else state.live_cell_count--;
        
        // 标记块为脏
        int chunk_x = world_x / CHUNK_SIZE;
        int chunk_y = world_y / CHUNK_SIZE;
        state.dirty_chunks.push_back({chunk_x, chunk_y});
    }
}

// 初始化游戏状态
void init_game(GameState &state, int argc, char** argv) {
    state.rows = LINES;
    state.cols = COLS;
    
    // 初始光标位置居中
    state.cursor_screen_x = state.cols / 2;
    state.cursor_screen_y = state.rows / 2;
    
    // 检查是否需要预计算
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-z") == 0) {
            state.precompute = true;
            // 检查是否有指定轮数
            if (i + 1 < argc) {
                char* end;
                long rounds = strtol(argv[i+1], &end, 10);
                if (*end == '\0' && rounds > 0 && rounds <= 1000) {
                    state.precompute_rounds = rounds;
                    i++; // 跳过下一个参数
                }
            }
        }
    }
}

// 计算下一代（使用基于块的优化）
void compute_generation(GameState &state) {
    if (state.live_cell_count == 0) return; // 没有活细胞，跳过计算
    
    // 仅存储需要更新的细胞
    vector<tuple<int, int, bool>> updates;
    unordered_map<int, unordered_map<int, bool>> processed_chunks;
    
    // 只处理有活细胞的块及其邻居
    for (auto& [chunk_y, row] : state.world) {
        for (auto& [chunk_x, chunk] : row) {
            if (chunk == nullptr) continue;
            
            // 处理当前块及其周围8个块
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int cy = chunk_y + dy;
                    int cx = chunk_x + dx;
                    
                    // 避免重复处理
                    if (processed_chunks.find(cy) != processed_chunks.end() && 
                        processed_chunks[cy].find(cx) != processed_chunks[cy].end()) {
                        continue;
                    }
                    
                    processed_chunks[cy][cx] = true;
                    
                    // 处理块中的每个细胞
                    for (int y = 0; y < CHUNK_SIZE; y++) {
                        for (int x = 0; x < CHUNK_SIZE; x++) {
                            int world_x = cx * CHUNK_SIZE + x;
                            int world_y = cy * CHUNK_SIZE + y;
                            
                            // 计算邻居数量
                            int neighbors = 0;
                            for (int ny = -1; ny <= 1; ny++) {
                                for (int nx = -1; nx <= 1; nx++) {
                                    if (nx == 0 && ny == 0) continue;
                                    if (peek_cell(state, world_x + nx, world_y + ny)) {
                                        neighbors++;
                                    }
                                }
                            }
                            
                            bool current = peek_cell(state, world_x, world_y);
                            if (current) {
                                if (neighbors < 2 || neighbors > 3) {
                                    updates.push_back({world_x, world_y, false});
                                }
                            } else {
                                if (neighbors == 3) {
                                    updates.push_back({world_x, world_y, true});
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 应用更新
    for (auto& [x, y, alive] : updates) {
        set_cell(state, x, y, alive);
    }
}

// 显示加载页面（简化版）
void show_loading(GameState &state) {
    clear();
    int width = min(30, state.cols - 10); // 更小的进度条
    int start_col = (state.cols - width) / 2;
    int start_row = state.rows / 2;
    
    mvprintw(start_row - 2, start_col, "Precomputing %d rounds...", state.precompute_rounds);
    mvprintw(start_row, start_col - 1, "[");
    mvprintw(start_row, start_col + width, "]");
    refresh();
    
    // 计算刷新间隔（避免太多刷新影响性能）
    int refresh_interval = max(1, state.precompute_rounds / 50);
    
    for (int i = 0; i < state.precompute_rounds; i++) {
        compute_generation(state);
        
        // 定期刷新进度条
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
    
    // 绘制完整进度条
    for (int j = 0; j < width; j++) {
        mvaddch(start_row, start_col + j, '=' | A_REVERSE);
    }
    mvprintw(start_row + 2, start_col, "Progress: 100%%");
    refresh();
    
    // 短暂暂停让用户看到完成状态
    this_thread::sleep_for(chrono::milliseconds(200));
    
    // 清除加载信息
    for (int r = start_row - 2; r <= start_row + 2; r++) {
        move(r, 0);
        clrtoeol();
    }
    refresh();
}

// 绘制一个块
void draw_chunk(GameState& state, int chunk_x, int chunk_y, Chunk* chunk) {
    int world_start_x = chunk_x * CHUNK_SIZE;
    int world_start_y = chunk_y * CHUNK_SIZE;
    
    for (int y = 0; y < CHUNK_SIZE; y++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {
            int screen_x = world_start_x + x - state.viewport_x;
            int screen_y = world_start_y + y - state.viewport_y;
            
            if (screen_x >= 0 && screen_x < state.cols && 
                screen_y >= 0 && screen_y < state.rows) {
                
                if (chunk->cells[y][x]) {
                    mvaddch(screen_y, screen_x, '#' | A_BOLD);
                } else {
                    mvaddch(screen_y, screen_x, ' ');
                }
            }
        }
    }
    
    chunk->dirty = false;
}

// 修复光标痕迹问题（使用peek_cell避免创建新块）
void draw_cursor(GameState &state) {
    // 清除上一次光标的痕迹
    if (state.prev_cursor_screen_y >= 0 && state.prev_cursor_screen_y < state.rows && 
        state.prev_cursor_screen_x >= 0 && state.prev_cursor_screen_x < state.cols) {
        
        // 计算上一次位置的世界坐标
        int world_x = state.prev_cursor_screen_x + state.viewport_x;
        int world_y = state.prev_cursor_screen_y + state.viewport_y;
        
        // 使用peek_cell避免创建新块
        bool isAlive = peek_cell(state, world_x, world_y);
        chtype ch = isAlive ? '#' : ' ';
        
        // 在旧位置正常绘制细胞
        mvaddch(state.prev_cursor_screen_y, state.prev_cursor_screen_x, ch);
    }
    
    // 保存当前光标位置作为下一次的上一次位置
    state.prev_cursor_screen_x = state.cursor_screen_x;
    state.prev_cursor_screen_y = state.cursor_screen_y;
    
    // 在新位置绘制光标（反色显示）
    if (state.cursor_screen_y >= 0 && state.cursor_screen_y < state.rows && 
        state.cursor_screen_x >= 0 && state.cursor_screen_x < state.cols) {
        
        // 计算当前位置的世界坐标
        int world_x = state.cursor_screen_x + state.viewport_x;
        int world_y = state.cursor_screen_y + state.viewport_y;
        
        // 使用peek_cell避免创建新块
        bool isAlive = peek_cell(state, world_x, world_y);
        chtype ch = isAlive ? '#' : ' ';
        
        // 在新位置反色显示
        mvaddch(state.cursor_screen_y, state.cursor_screen_x, ch | A_REVERSE);
    }
}

// 绘制所有可见块
void draw_all_visible_chunks(GameState &state) {
    // 计算视口范围（世界坐标）
    int min_world_x = state.viewport_x;
    int min_world_y = state.viewport_y;
    int max_world_x = state.viewport_x + state.cols;
    int max_world_y = state.viewport_y + state.rows;
    
    // 转换为块坐标
    int min_chunk_x = min_world_x / CHUNK_SIZE;
    int min_chunk_y = min_world_y / CHUNK_SIZE;
    int max_chunk_x = max_world_x / CHUNK_SIZE + 1;
    int max_chunk_y = max_world_y / CHUNK_SIZE + 1;
    
    // 遍历所有可能包含可见块的块
    for (int chunk_y = min_chunk_y; chunk_y <= max_chunk_y; chunk_y++) {
        if (state.world.find(chunk_y) == state.world.end()) continue;
        
        for (int chunk_x = min_chunk_x; chunk_x <= max_chunk_x; chunk_x++) {
            if (state.world[chunk_y].find(chunk_x) == state.world[chunk_y].end()) continue;
            
            // 绘制这个块
            draw_chunk(state, chunk_x, chunk_y, state.world[chunk_y][chunk_x]);
        }
    }
}

// 设计模式
void design_mode(GameState &state) {
    curs_set(1); // 显示光标
    state.dirty_chunks.clear(); // 清除脏块列表
    
    // 初始化上一次光标位置
    state.prev_cursor_screen_x = state.cursor_screen_x;
    state.prev_cursor_screen_y = state.cursor_screen_y;
    
    while (state.mode == DESIGN) {
        if (state.viewport_changed || state.need_full_refresh) {
            // 视口改变或需要完全刷新，重绘所有可见块
            clear();
            draw_all_visible_chunks(state);
            state.viewport_changed = false;
            state.need_full_refresh = false;
            state.dirty_chunks.clear(); // 清除脏块列表，因为已经全部重绘
        } else {
            // 只绘制脏块
            for (auto& [chunk_x, chunk_y] : state.dirty_chunks) {
                if (state.world.find(chunk_y) != state.world.end() && 
                    state.world[chunk_y].find(chunk_x) != state.world[chunk_y].end()) {
                    draw_chunk(state, chunk_x, chunk_y, state.world[chunk_y][chunk_x]);
                }
            }
            state.dirty_chunks.clear();
        }
        
        // 绘制光标（处理痕迹问题）
        draw_cursor(state);
        
        // 显示模式信息和坐标
        mvprintw(0, 0, "DESIGN MODE - Cells: %d | Cursor: (%d, %d) | Viewport: (%d, %d)", 
                 state.live_cell_count, 
                 state.cursor_screen_x + state.viewport_x, 
                 state.cursor_screen_y + state.viewport_y,
                 state.viewport_x, state.viewport_y);
        clrtoeol();
        refresh();
        
        // 处理输入
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
            case 'w': case 'W': // WASD 移动地图
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
            case KEY_UP: // 方向键移动光标
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
            case '\n': case ' ': // 回车或空格反转细胞状态
            {
                // 计算光标位置的世界坐标
                int world_x = state.cursor_screen_x + state.viewport_x;
                int world_y = state.cursor_screen_y + state.viewport_y;
                
                // 使用peek_cell获取当前状态（不创建新块）
                bool current = peek_cell(state, world_x, world_y);
                
                // 使用set_cell设置新状态（会创建块）
                set_cell(state, world_x, world_y, !current);
                break;
            }
        }
    }
}

// 命令模式（简化版）
void command_mode(GameState &state) {
    curs_set(1); // 显示光标
    echo();      // 开启回显
    
    while (state.mode == COMMAND) {
        // 显示命令提示
        move(state.rows - 1, 0);
        clrtoeol();
        printw("CMD:%s", state.command_str.c_str());
        move(state.rows - 1, 5 + state.command_str.size());
        refresh();
        
        // 处理输入
        int ch = getch();
        switch (ch) {
            case 27: // ESC
                state.mode = DESIGN;
                break;
            case '\n': // 执行命令
                if (state.command_str.substr(0, 4) == "rand") {
                    int x, y, w, h;
                    if (sscanf(state.command_str.c_str(), "rand %d %d %d %d", 
                               &x, &y, &w, &h) == 4) {
                        // 生成随机结构
                        for (int i = y; i < y + h; i++) {
                            for (int j = x; j < x + w; j++) {
                                if (rand() % 3 == 0) { // 1/3的概率
                                    set_cell(state, j, i, true);
                                }
                            }
                        }
                        // 设置需要完全刷新视口
                        state.need_full_refresh = true;
                    }
                }
                state.mode = DESIGN;
                break;
            case 127: case KEY_BACKSPACE: // 退格
                if (!state.command_str.empty()) {
                    state.command_str.pop_back();
                }
                break;
            default:
                if (ch >= 32 && ch <= 126) { // 可打印字符
                    state.command_str += ch;
                }
                break;
        }
    }
    
    noecho(); // 关闭回显
}

// 演算模式（高度优化）
void play_mode(GameState &state) {
    curs_set(0); // 隐藏光标
    nodelay(stdscr, TRUE); // 非阻塞输入
    state.dirty_chunks.clear();
    
    while (state.mode == PLAY) {
        auto start_time = chrono::steady_clock::now();
        
        // 计算下一代（只在有活细胞时）
        if (state.live_cell_count > 0) {
            compute_generation(state);
        }
        
        if (state.viewport_changed || state.need_full_refresh) {
            // 视口改变或需要完全刷新，重绘所有可见块
            clear();
            draw_all_visible_chunks(state);
            state.viewport_changed = false;
            state.need_full_refresh = false;
            state.dirty_chunks.clear(); // 清除脏块列表，因为已经全部重绘
        } else {
            // 只绘制脏块
            for (auto& [chunk_x, chunk_y] : state.dirty_chunks) {
                if (state.world.find(chunk_y) != state.world.end() && 
                    state.world[chunk_y].find(chunk_x) != state.world[chunk_y].end()) {
                    draw_chunk(state, chunk_x, chunk_y, state.world[chunk_y][chunk_x]);
                }
            }
            state.dirty_chunks.clear();
        }
        
        // 显示模式信息
        mvprintw(0, 0, "PLAY MODE - Cells: %d | Viewport: (%d, %d)", 
                 state.live_cell_count, state.viewport_x, state.viewport_y);
        clrtoeol();
        refresh();
        
        // 处理输入
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
        
        // 自适应延迟，保持约10FPS
        auto frame_time = chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - start_time).count();
        
        int delay = max(0, 100 - (int)frame_time);
        this_thread::sleep_for(chrono::milliseconds(delay));
    }
    
    nodelay(stdscr, FALSE); // 恢复阻塞输入
}

int main(int argc, char** argv) {
    // 初始化ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(0);
    start_color(); // 启用颜色支持
    use_default_colors(); // 使用终端默认颜色
    
    // 初始化随机种子
    srand(time(nullptr));
    
    // 初始化游戏状态
    GameState state;
    init_game(state, argc, argv);
    
    // 主循环
    while (state.running) {
        switch (state.mode) {
            case DESIGN:
                design_mode(state);
                break;
            case COMMAND:
                command_mode(state);
                break;
            case PLAY:
                play_mode(state);
                break;
        }
    }
    
    // 清理内存
    for (auto& [y, row] : state.world) {
        for (auto& [x, chunk] : row) {
            delete chunk;
        }
    }
    
    // 清理ncurses
    endwin();
    return 0;
}