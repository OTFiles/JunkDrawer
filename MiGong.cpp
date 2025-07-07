// g++ -o MG MiGong.cpp -lncurses

#include <ncurses.h>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <stack>
#include <random>

const int MAZE_ROWS = 15;
const int MAZE_COLS = 30;

struct Player {
    int x, y;
};

// 使用递归回溯算法生成迷宫
std::vector<std::vector<char>> generate_maze() {
    std::vector<std::vector<char>> maze(MAZE_ROWS, std::vector<char>(MAZE_COLS, '#'));
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    std::stack<std::pair<int, int>> path;
    
    // 从固定起点开始确保连通性
    int start_x = 1;
    int start_y = 1;
    
    maze[start_y][start_x] = ' ';
    path.push({start_x, start_y});
    
    int dx[] = {2, -2, 0, 0};
    int dy[] = {0, 0, 2, -2};
    
    while (!path.empty()) {
        int x = path.top().first;
        int y = path.top().second;
        
        std::vector<int> directions = {0, 1, 2, 3};
        std::shuffle(directions.begin(), directions.end(), gen);
        
        bool found = false;
        for (int dir : directions) {
            int nx = x + dx[dir];
            int ny = y + dy[dir];
            
            if (nx > 0 && nx < MAZE_COLS-1 && ny > 0 && ny < MAZE_ROWS-1 && maze[ny][nx] == '#') {
                // 打通当前位置到新位置的路径
                maze[y + dy[dir]/2][x + dx[dir]/2] = ' ';
                maze[ny][nx] = ' ';
                path.push({nx, ny});
                found = true;
                break;
            }
        }
        
        if (!found) {
            path.pop();
        }
    }
    
    // 确保起点和出口
    maze[1][1] = ' '; // 起点
    maze[MAZE_ROWS-2][MAZE_COLS-2] = 'E'; // 出口
    
    return maze;
}

// 绘制整个迷宫
void draw_maze(const std::vector<std::vector<char>>& maze, const Player& player) {
    clear(); // 清除屏幕
    
    for (int y = 0; y < MAZE_ROWS; y++) {
        for (int x = 0; x < MAZE_COLS; x++) {
            if (maze[y][x] == '#') {
                attron(COLOR_PAIR(2));
                mvaddch(y, x, '#');
                attroff(COLOR_PAIR(2));
            } else if (maze[y][x] == 'E') {
                attron(COLOR_PAIR(3) | A_BOLD);
                mvaddch(y, x, 'E');
                attroff(COLOR_PAIR(3) | A_BOLD);
            } else {
                mvaddch(y, x, ' ');
            }
        }
    }
    
    // 绘制玩家
    attron(COLOR_PAIR(1) | A_BOLD);
    mvaddch(player.y, player.x, '@');
    attroff(COLOR_PAIR(1) | A_BOLD);
    
    refresh();
}

int main() {
    // 初始化ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0); // 隐藏光标
    nodelay(stdscr, TRUE); // 非阻塞输入
    
    // 检查颜色支持
    if (!has_colors()) {
        endwin();
        printf("您的终端不支持颜色!\n");
        return 1;
    }
    
    // 初始化颜色
    start_color();
    init_pair(1, COLOR_GREEN, COLOR_BLACK); // 玩家颜色
    init_pair(2, COLOR_CYAN, COLOR_BLACK);  // 迷宫墙壁颜色
    init_pair(3, COLOR_YELLOW, COLOR_BLACK); // 出口颜色
    
    // 初始玩家位置
    Player player = {1, 1};
    
    // 初始生成迷宫
    auto maze = generate_maze();
    
    // 初始绘制整个迷宫
    draw_maze(maze, player);
    
    // 游戏主循环
    int ch;
    bool game_running = true;
    while (game_running) {
        ch = getch();
        if (ch == 'q') {
            game_running = false;
            break;
        }
        
        // 保存旧位置
        int old_x = player.x;
        int old_y = player.y;
        
        // 计算新位置
        int new_x = old_x;
        int new_y = old_y;
        
        switch (ch) {
            case KEY_UP:    new_y--; break;
            case KEY_DOWN:  new_y++; break;
            case KEY_LEFT:  new_x--; break;
            case KEY_RIGHT: new_x++; break;
            default: 
                // 无有效输入时继续循环
                usleep(10000);
                continue;
        }
        
        // 边界检查
        if (new_x < 0) new_x = 0;
        if (new_x >= MAZE_COLS) new_x = MAZE_COLS - 1;
        if (new_y < 0) new_y = 0;
        if (new_y >= MAZE_ROWS) new_y = MAZE_ROWS - 1;
        
        // 检查移动是否有效（不能穿越墙壁）
        if (maze[new_y][new_x] != '#') {
            // 只有目标位置不是墙壁才允许移动
            player.x = new_x;
            player.y = new_y;
        }
        
        // 检查胜利条件（到达出口）
        if (maze[player.y][player.x] == 'E') {
            // 显示胜利消息
            attron(A_BOLD);
            mvprintw(MAZE_ROWS/2, MAZE_COLS/2 - 5, "胜利!");
            attroff(A_BOLD);
            refresh();
            usleep(2000000);
            game_running = false;
            // 退出循环，不再重新生成迷宫
            break;
        }
        
        // 每次移动后重新生成迷宫（除非已经胜利）
        maze = generate_maze();
        
        // 确保玩家位置是空地
        if (maze[player.y][player.x] == '#') {
            maze[player.y][player.x] = ' ';
        }
        
        // 确保出口存在
        if (maze[MAZE_ROWS-2][MAZE_COLS-2] != 'E') {
            maze[MAZE_ROWS-2][MAZE_COLS-2] = 'E';
        }
        
        // 重新绘制整个迷宫
        draw_maze(maze, player);
        
        // 小延迟减少CPU使用
        usleep(10000);
    }
    
    endwin(); // 结束ncurses
    return 0;
}