// g++ -o LifeGame LifeGame.cpp -lncurses -pthread
// ./LifeGame  =>  普通模式
// ./LifeGame  =>  在展示演算结果之前先演算100轮再展示

#include <ncurses.h>
#include <vector>
#include <string>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <thread>
#include <chrono>
using namespace std;

enum Mode { DESIGN, COMMAND, PLAY };

struct GameState {
    Mode mode = DESIGN;
    int rows, cols;
    vector<vector<bool>> grid1, grid2;
    vector<vector<bool>> *current, *next;
    int cursor_y, cursor_x;
    string command_str;
    bool precompute = false;
    int precompute_rounds = 50;
    bool running = true;
};

// 初始化游戏状态
void init_game(GameState &state, int argc, char** argv) {
    state.rows = LINES;
    state.cols = COLS;
    
    // 初始化网格
    state.grid1 = vector<vector<bool>>(state.rows, vector<bool>(state.cols, false));
    state.grid2 = vector<vector<bool>>(state.rows, vector<bool>(state.cols, false));
    state.current = &state.grid1;
    state.next = &state.grid2;
    
    // 初始光标位置居中
    state.cursor_y = state.rows / 2;
    state.cursor_x = state.cols / 2;
    
    // 检查是否需要预计算
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-z") == 0) {
            state.precompute = true;
        }
    }
}

// 交换双缓冲区
void swap_buffers(GameState &state) {
    swap(state.current, state.next);
}

// 计算邻居数量（使用边界检查）
int count_neighbors(GameState &state, int y, int x) {
    int count = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dy == 0 && dx == 0) continue;
            int ny = y + dy;
            int nx = x + dx;
            if (ny >= 0 && ny < state.rows && nx >= 0 && nx < state.cols) {
                if ((*state.current)[ny][nx]) {
                    count++;
                }
            }
        }
    }
    return count;
}

// 计算下一代
void compute_generation(GameState &state) {
    for (int y = 0; y < state.rows; y++) {
        for (int x = 0; x < state.cols; x++) {
            int neighbors = count_neighbors(state, y, x);
            if ((*state.current)[y][x]) {
                (*state.next)[y][x] = (neighbors == 2 || neighbors == 3);
            } else {
                (*state.next)[y][x] = (neighbors == 3);
            }
        }
    }
    swap_buffers(state);
}

// 显示加载页面
void show_loading(GameState &state) {
    clear();
    int width = min(50, state.cols - 10);
    int start_col = (state.cols - width) / 2;
    int start_row = state.rows / 2;
    
    mvprintw(start_row - 2, start_col, "Precomputing %d rounds...", state.precompute_rounds);
    mvprintw(start_row, start_col - 1, "[");
    mvprintw(start_row, start_col + width, "]");
    
    for (int i = 0; i < state.precompute_rounds; i++) {
        compute_generation(state);
        int progress = (i + 1) * width / state.precompute_rounds;
        
        for (int j = 0; j < progress; j++) {
            mvaddch(start_row, start_col + j, '=' | A_REVERSE);
        }
        mvprintw(start_row + 2, start_col, "Progress: %d%%", (i + 1) * 100 / state.precompute_rounds);
        refresh();
        this_thread::sleep_for(chrono::milliseconds(20));
    }
    
    // 清除加载信息
    for (int r = start_row - 2; r <= start_row + 2; r++) {
        move(r, 0);
        clrtoeol();
    }
}

// 设计模式
void design_mode(GameState &state) {
    curs_set(1); // 显示光标
    
    while (state.mode == DESIGN) {
        clear();
        
        // 绘制网格
        for (int y = 0; y < state.rows; y++) {
            for (int x = 0; x < state.cols; x++) {
                if ((*state.current)[y][x]) {
                    mvaddch(y, x, '#' | A_BOLD);
                }
            }
        }
        
        // 绘制光标（反色显示）
        if (state.cursor_y >= 0 && state.cursor_y < state.rows && 
            state.cursor_x >= 0 && state.cursor_x < state.cols) {
            chtype ch = (*state.current)[state.cursor_y][state.cursor_x] ? '#' : ' ';
            mvaddch(state.cursor_y, state.cursor_x, ch | A_REVERSE);
        }
        
        // 显示模式信息
        mvprintw(0, 0, "DESIGN MODE - [C]ommand [Y]Play [Q]uit");
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
            case KEY_UP:
                if (state.cursor_y > 0) state.cursor_y--;
                break;
            case KEY_DOWN:
                if (state.cursor_y < state.rows - 1) state.cursor_y++;
                break;
            case KEY_LEFT:
                if (state.cursor_x > 0) state.cursor_x--;
                break;
            case KEY_RIGHT:
                if (state.cursor_x < state.cols - 1) state.cursor_x++;
                break;
            case '\n': case ' ': // 回车或空格反转细胞状态
                if (state.cursor_y >= 0 && state.cursor_y < state.rows && 
                    state.cursor_x >= 0 && state.cursor_x < state.cols) {
                    (*state.current)[state.cursor_y][state.cursor_x] = 
                        !(*state.current)[state.cursor_y][state.cursor_x];
                }
                break;
        }
    }
}

// 命令模式
void command_mode(GameState &state) {
    curs_set(1); // 显示光标
    echo();      // 开启回显
    
    while (state.mode == COMMAND) {
        // 绘制网格
        for (int y = 0; y < state.rows - 1; y++) {
            move(y, 0);
            for (int x = 0; x < state.cols; x++) {
                if ((*state.current)[y][x]) {
                    addch('#' | A_BOLD);
                } else {
                    addch(' ');
                }
            }
        }
        
        // 显示命令提示
        move(state.rows - 1, 0);
        clrtoeol();
        printw(":%s", state.command_str.c_str());
        move(state.rows - 1, 1 + state.command_str.size());
        refresh();
        
        // 处理输入
        int ch = getch();
        switch (ch) {
            case 27: // ESC
                state.mode = DESIGN;
                break;
            case '\n': // 执行命令
                if (state.command_str.substr(0, 4) == "rand") {
                    int y1, x1, y2, x2;
                    if (sscanf(state.command_str.c_str(), "rand %d %d %d %d", 
                               &y1, &x1, &y2, &x2) == 4) {
                        // 确保坐标在有效范围内
                        y1 = max(0, min(y1, state.rows - 1));
                        x1 = max(0, min(x1, state.cols - 1));
                        y2 = max(0, min(y2, state.rows - 1));
                        x2 = max(0, min(x2, state.cols - 1));
                        
                        // 确保y1<=y2, x1<=x2
                        if (y1 > y2) swap(y1, y2);
                        if (x1 > x2) swap(x1, x2);
                        
                        // 生成随机结构
                        for (int y = y1; y <= y2; y++) {
                            for (int x = x1; x <= x2; x++) {
                                (*state.current)[y][x] = (rand() % 2 == 0);
                            }
                        }
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

// 演算模式
void play_mode(GameState &state) {
    curs_set(0); // 隐藏光标
    nodelay(stdscr, TRUE); // 非阻塞输入
    
    while (state.mode == PLAY) {
        // 绘制网格（仅绘制活细胞优化性能）
        for (int y = 0; y < state.rows; y++) {
            for (int x = 0; x < state.cols; x++) {
                if ((*state.current)[y][x]) {
                    mvaddch(y, x, '#' | A_BOLD);
                } else {
                    // 使用空格覆盖之前的内容
                    mvaddch(y, x, ' ');
                }
            }
        }
        
        // 显示模式信息
        mvprintw(0, 0, "PLAY MODE - [Q] to design");
        clrtoeol();
        refresh();
        
        // 计算下一代
        compute_generation(state);
        
        // 处理输入
        int ch = getch();
        if (ch == 'q' || ch == 'Q') {
            state.mode = DESIGN;
        }
        
        // 控制速度
        this_thread::sleep_for(chrono::milliseconds(50));
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
    
    // 清理ncurses
    endwin();
    return 0;
}