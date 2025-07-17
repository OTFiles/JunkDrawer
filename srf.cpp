// g++ -o SRF srf.cpp -lncurses -lutil
#include <curses.h>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <algorithm>
#include <pty.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <poll.h>

// 输入法状态结构
struct InputMethodState {
    bool is_chinese;            // 当前是否为中文模式
    std::string input_buffer;   // 输入的拼音字符串
    std::vector<std::string> candidates; // 候选词列表
    int selected_index;         // 当前选中的候选词索引
    int page_start;             // 当前页的起始索引
    int page_size;              // 每页显示的候选词数量
};

// 全局变量
std::map<std::string, std::vector<std::string>> dictionary; // 拼音字典
InputMethodState im_state;
int pty_master;                // 伪终端主设备
int orig_lines, orig_cols;     // 原始终端尺寸
struct termios orig_termios;   // 原始终端设置
bool resize_occurred = false;  // 终端大小改变标志
pid_t child_pid = -1;          // 子进程PID
volatile sig_atomic_t child_exited = 0; // 子进程退出标志

// 颜色定义 - 使用ANSI颜色
#define INPUT_COLOR "\033[38;5;15;48;5;21m"      // 白字蓝底
#define CANDIDATE_COLOR "\033[38;5;0;48;5;51m"   // 黑字青底
#define SELECTED_COLOR "\033[38;5;11;48;5;201m"  // 黄字紫底
#define STATUS_COLOR "\033[38;5;11;48;5;21m"     // 黄字蓝底
#define RESET_COLOR "\033[0m"                    // 重置颜色

// 信号处理函数：处理终端大小改变
void handle_sigwinch(int sig) {
    resize_occurred = true;
}

// 信号处理函数：处理子进程退出
void handle_sigchld(int sig) {
    child_exited = 1;
}

// 恢复终端设置
void restore_terminal() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    // 清除输入法区域并重置光标
    printf("\033[%d;1H\033[0J", orig_lines);
    printf("\033[?25h"); // 显示光标
    fflush(stdout);
}

// 清理并退出
void cleanup_exit(int sig) {
    if (child_pid > 0) {
        kill(child_pid, SIGTERM);
        int status;
        waitpid(child_pid, &status, 0);
    }
    restore_terminal();
    _exit(sig);
}

// 加载配置文件
void load_config(const char* filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') continue;
        
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        
        std::string key = line.substr(0, pos);
        std::string values = line.substr(pos + 1);
        
        // 分割候选词
        std::vector<std::string> candidate_list;
        std::istringstream iss(values);
        std::string word;
        while (iss >> word) {
            candidate_list.push_back(word);
        }
        
        dictionary[key] = candidate_list;
    }
}

// 更新候选词列表
void update_candidates() {
    im_state.candidates.clear();
    
    auto it = dictionary.find(im_state.input_buffer);
    if (it != dictionary.end()) {
        im_state.candidates = it->second;
    }
    
    im_state.selected_index = 0;
    im_state.page_start = 0;
}

// 发送字符串到子进程
void send_to_child(const std::string& str) {
    if (pty_master != -1) {
        write(pty_master, str.c_str(), str.size());
    }
}

// 绘制输入法界面
void draw_ime() {
    // 保存光标位置
    printf("\0337"); // DECSC - 保存光标位置
    
    // 定位到倒数第二行并清除
    printf("\033[%d;1H\033[K", orig_lines - 1);
    printf(INPUT_COLOR "输入: %s", im_state.input_buffer.c_str());
    
    // 填充剩余空间
    int used = 8 + im_state.input_buffer.size();
    for (int i = used; i < orig_cols - 10; i++) {
        putchar(' ');
    }
    
    // 显示中英文状态
    printf(STATUS_COLOR "[%s]" RESET_COLOR, im_state.is_chinese ? "中文" : "英文");
    
    // 定位到最后一行并清除
    printf("\033[%d;1H\033[K", orig_lines);
    
    // 绘制候选词
    if (im_state.is_chinese && !im_state.candidates.empty()) {
        int start = im_state.page_start;
        int end = std::min(start + im_state.page_size, (int)im_state.candidates.size());
        
        for (int i = start; i < end; i++) {
            // 高亮显示选中的候选词
            if (i == im_state.selected_index) {
                printf(SELECTED_COLOR);
            } else {
                printf(CANDIDATE_COLOR);
            }
            
            // 显示候选词和编号
            int display_num = (i - start + 1) % 10;
            char num_char = (display_num == 0) ? '0' : ('0' + display_num);
            printf("%c. %s ", num_char, im_state.candidates[i].c_str());
        }
        printf(RESET_COLOR);
    }
    
    // 恢复光标位置
    printf("\0338"); // DECRC - 恢复光标位置
    fflush(stdout);
}

// 处理按键输入
void handle_input(char ch) {
    // Ctrl+Z 切换中英文模式
    if (ch == 0x1A) {
        im_state.is_chinese = !im_state.is_chinese;
        im_state.input_buffer.clear();
        im_state.candidates.clear();
        draw_ime();
        return;
    }
    
    if (!im_state.is_chinese) {
        // 英文模式：直接传递所有输入
        char c = ch;
        send_to_child(std::string(&c, 1));
        return;
    }
    
    // 中文模式处理
    if (ch == '\r' || ch == '\n') {
        // 回车键：提交当前输入缓冲区作为英文输入
        if (!im_state.input_buffer.empty()) {
            send_to_child(im_state.input_buffer);
            im_state.input_buffer.clear();
            im_state.candidates.clear();
        }
        // 发送回车
        send_to_child("\r");
        draw_ime();
    } else if (ch == 0x7F || ch == 0x08) { // Backspace 或 Delete
        if (!im_state.input_buffer.empty()) {
            im_state.input_buffer.pop_back();
            update_candidates();
            draw_ime();
        }
    } else if (ch == ' ') {
        // 空格键选择当前候选词
        if (!im_state.candidates.empty()) {
            send_to_child(im_state.candidates[im_state.selected_index]);
            im_state.input_buffer.clear();
            im_state.candidates.clear();
            draw_ime();
        } else {
            send_to_child(" ");
        }
    } else if (ch >= '1' && ch <= '9') {
        // 数字键选择候选词
        int idx = ch - '1';
        int actual_index = im_state.page_start + idx;
        if (actual_index < im_state.candidates.size()) {
            send_to_child(im_state.candidates[actual_index]);
            im_state.input_buffer.clear();
            im_state.candidates.clear();
            draw_ime();
        }
    } else if (ch == '0') {
        // 0选择第10个候选词
        int actual_index = im_state.page_start + 9;
        if (actual_index < im_state.candidates.size()) {
            send_to_child(im_state.candidates[actual_index]);
            im_state.input_buffer.clear();
            im_state.candidates.clear();
            draw_ime();
        }
    } else if (ch == 0x1B) { // ESC
        // 处理方向键
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return;
        
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': // 上箭头
                    if (im_state.page_start >= im_state.page_size) {
                        im_state.page_start -= im_state.page_size;
                        im_state.selected_index = im_state.page_start;
                        draw_ime();
                    }
                    break;
                case 'B': // 下箭头
                    if (im_state.page_start + im_state.page_size < im_state.candidates.size()) {
                        im_state.page_start += im_state.page_size;
                        im_state.selected_index = im_state.page_start;
                        draw_ime();
                    }
                    break;
                case 'C': // 右箭头
                    if (im_state.selected_index < im_state.candidates.size() - 1) {
                        im_state.selected_index++;
                        if (im_state.selected_index >= im_state.page_start + im_state.page_size) {
                            im_state.page_start += im_state.page_size;
                        }
                        draw_ime();
                    }
                    break;
                case 'D': // 左箭头
                    if (im_state.selected_index > 0) {
                        im_state.selected_index--;
                        if (im_state.selected_index < im_state.page_start) {
                            im_state.page_start -= im_state.page_size;
                        }
                        draw_ime();
                    }
                    break;
            }
        }
    } else if (isalpha(ch)) {
        // 字母键添加到输入缓冲区
        im_state.input_buffer += ch;
        update_candidates();
        draw_ime();
    } else {
        // 其他字符直接发送
        char c = ch;
        send_to_child(std::string(&c, 1));
    }
}

// 创建伪终端并运行子进程
void run_child_process(char* argv[]) {
    // 创建伪终端
    int master_fd;
    struct winsize ws;
    
    // 获取终端尺寸
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
    ws.ws_row -= 2; // 减去输入法区域
    
    pid_t pid = forkpty(&master_fd, NULL, NULL, &ws);
    
    if (pid < 0) {
        perror("forkpty");
        exit(1);
    } else if (pid == 0) {
        // 子进程执行命令
        setenv("TERM", "xterm-256color", 1); // 设置终端类型
        
        // 确保子进程在正确的行数上运行
        struct winsize child_ws;
        ioctl(STDIN_FILENO, TIOCGWINSZ, &child_ws);
        child_ws.ws_row -= 2;
        ioctl(STDIN_FILENO, TIOCSWINSZ, &child_ws);
        
        execvp(argv[0], argv);
        perror("execvp");
        exit(1);
    }
    
    pty_master = master_fd;
    child_pid = pid;
}

// 调整终端大小
void resize_terminal() {
    struct winsize ws;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
    
    orig_lines = ws.ws_row;
    orig_cols = ws.ws_col;
    
    // 设置子进程终端大小
    ws.ws_row -= 2;
    ioctl(pty_master, TIOCSWINSZ, &ws);
    
    // 清除并重绘输入法区域
    draw_ime();
    resize_occurred = false;
}

// 清屏并重绘输入法
void reset_display() {
    // 清屏
    printf("\033[2J");
    // 移动光标到左上角
    printf("\033[H");
    
    // 通知子进程重绘
    struct winsize ws;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
    ws.ws_row -= 2;
    ioctl(pty_master, TIOCSWINSZ, &ws);
    
    // 重绘输入法
    draw_ime();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        return 1;
    }
    
    // 初始化输入法状态
    im_state.is_chinese = true;
    im_state.page_size = 8; // 根据终端宽度调整
    
    // 加载拼音字典
    load_config("srf.conf");
    
    // 获取原始终端设置
    tcgetattr(STDIN_FILENO, &orig_termios);
    
    // 设置信号处理
    signal(SIGWINCH, handle_sigwinch);
    signal(SIGCHLD, handle_sigchld);
    signal(SIGINT, cleanup_exit);
    signal(SIGTERM, cleanup_exit);
    signal(SIGHUP, cleanup_exit);
    
    // 设置原始模式
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    
    // 获取终端尺寸
    struct winsize ws;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
    orig_lines = ws.ws_row;
    orig_cols = ws.ws_col;
    
    // 隐藏光标
    printf("\033[?25l");
    fflush(stdout);
    
    // 运行子进程
    run_child_process(&argv[1]);
    
    // 设置非阻塞IO
    fcntl(pty_master, F_SETFL, O_NONBLOCK);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    
    // 初始清屏
    reset_display();
    
    // 主事件循环
    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = pty_master;
    fds[1].events = POLLIN;
    
    char buffer[4096];
    int n;
    
    while (true) {
        if (child_exited) {
            // 子进程已退出，读取剩余输出
            while ((n = read(pty_master, buffer, sizeof(buffer)))) {
                if (n > 0) {
                    write(STDOUT_FILENO, buffer, n);
                } else if (n == 0 || (n < 0 && errno != EAGAIN)) {
                    break;
                }
            }
            break;
        }
        
        if (resize_occurred) {
            resize_terminal();
        }
        
        int ret = poll(fds, 2, 100); // 100ms超时
        
        if (ret < 0) {
            if (errno == EINTR) continue; // 信号中断
            perror("poll");
            break;
        }
        
        // 处理键盘输入
        if (fds[0].revents & POLLIN) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) == 1) {
                handle_input(ch);
            }
        }
        
        // 处理子进程输出
        if (fds[1].revents & POLLIN) {
            n = read(pty_master, buffer, sizeof(buffer));
            if (n > 0) {
                // 直接输出到终端
                write(STDOUT_FILENO, buffer, n);
            } else if (n == 0 || (n < 0 && errno != EAGAIN)) {
                // 子进程已退出
                break;
            }
        }
    }
    
    // 清理资源
    restore_terminal();
    close(pty_master);
    
    // 等待子进程结束
    int status;
    waitpid(child_pid, &status, 0);
    
    return 0;
}