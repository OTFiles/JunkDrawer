// g++ -o TUXmod TUXmod.cpp -lncurses

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <ncurses.h>
#include <map>
#include <ctime>
#include <filesystem>
#include <system_error>
#include <sys/stat.h>
#include <sys/types.h>
#include <chrono>
#include <cstddef>

// 全局颜色定义
#define HIGHLIGHT_COLOR 1
#define TITLE_COLOR 2
#define ERROR_COLOR 3
#define CHECKBOX_COLOR 4
#define INFO_COLOR 5

namespace fs = std::filesystem;

struct FileEntry {
    std::string name;
    bool is_directory;
    time_t mtime;
    off_t size;
};

// 目录缓存结构
struct DirCache {
    std::vector<FileEntry> files;
    time_t last_accessed;
};

// 全局缓存
static std::map<std::string, DirCache> dir_cache;
static time_t last_cache_clean = 0;

// 将文件系统时间转换为 time_t
static time_t file_time_to_time_t(const fs::file_time_type& ftime) {
    // 将文件系统时间转换为系统时钟时间
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    return std::chrono::system_clock::to_time_t(sctp);
}

// 清理过期缓存
void clean_old_cache() {
    time_t now = time(nullptr);
    if (now - last_cache_clean < 300) return; // 每5分钟最多清理一次
    
    for (auto it = dir_cache.begin(); it != dir_cache.end(); ) {
        if (now - it->second.last_accessed > 300) { // 超过5分钟未使用
            it = dir_cache.erase(it);
        } else {
            ++it;
        }
    }
    last_cache_clean = now;
}

// 获取目录修改时间
time_t get_dir_mtime(const std::string& path) {
    try {
        auto ftime = fs::last_write_time(path);
        return file_time_to_time_t(ftime);
    } catch (...) {
        return 0;
    }
}

// 读取目录内容（使用filesystem）
std::vector<FileEntry> read_directory(const std::string& path) {
    clean_old_cache();
    
    time_t current_mtime = get_dir_mtime(path);
    auto cache_it = dir_cache.find(path);
    
    // 检查缓存是否有效
    if (cache_it != dir_cache.end() && current_mtime != 0 && 
        cache_it->second.last_accessed >= current_mtime) {
        cache_it->second.last_accessed = time(nullptr); // 更新访问时间
        return cache_it->second.files;
    }
    
    std::vector<FileEntry> files;
    files.push_back({".", true, 0, 0});
    files.push_back({"..", true, 0, 0});

    try {
        for (const auto& entry : fs::directory_iterator(path, fs::directory_options::skip_permission_denied)) {
            std::string filename = entry.path().filename().string();
            
            try {
                bool is_dir = entry.is_directory();
                time_t mtime = file_time_to_time_t(entry.last_write_time());
                off_t size = is_dir ? 0 : entry.file_size();
                
                files.push_back({
                    filename,
                    is_dir,
                    mtime,
                    size
                });
            } catch (...) {
                // 跳过无法访问的文件
                continue;
            }
        }
    } catch (...) {
        // 目录访问错误
    }
    
    // 排序文件列表
    std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.is_directory != b.is_directory)
            return a.is_directory > b.is_directory;
        return a.name < b.name;
    });
    
    // 更新缓存
    DirCache cache_entry;
    cache_entry.files = files;
    cache_entry.last_accessed = time(nullptr);
    dir_cache[path] = cache_entry;
    
    return files;
}

// 文件选择器窗口
std::string file_selector(const std::string& start_dir = ".") {
    std::string current_dir = fs::absolute(start_dir).string();
    std::vector<FileEntry> files;
    int selected = 0;
    int scroll_pos = 0;
    
    // 初始化ncurses
    initscr();
    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0); // 隐藏光标
    nodelay(stdscr, TRUE); // 非阻塞输入
    init_pair(HIGHLIGHT_COLOR, COLOR_BLACK, COLOR_WHITE);
    init_pair(TITLE_COLOR, COLOR_WHITE, COLOR_BLUE);
    init_pair(ERROR_COLOR, COLOR_WHITE, COLOR_RED);
    init_pair(INFO_COLOR, COLOR_CYAN, COLOR_BLACK);
    init_pair(CHECKBOX_COLOR, COLOR_GREEN, COLOR_BLACK);

    // 初始读取目录
    files = read_directory(current_dir);
    
    // 主循环
    bool refresh_needed = true;
    while (true) {
        if (refresh_needed) {
            clear();
            
            // 计算可用显示行数
            int max_display = LINES - 4;
            if (max_display <= 0) max_display = 1;  // 确保至少显示一行
            
            // 显示标题
            attron(COLOR_PAIR(TITLE_COLOR));
            mvprintw(0, 0, "当前目录: %s", current_dir.c_str());
            clrtoeol();
            attroff(COLOR_PAIR(TITLE_COLOR));
            
            mvprintw(1, 0, "使用方向键选择，Enter确认，ESC取消，R刷新");
            clrtoeol();
            
            // 显示文件列表
            int display_count = std::min(max_display, (int)files.size() - scroll_pos);
            for (int i = 0; i < display_count; i++) {
                int idx = i + scroll_pos;
                int y = i + 2;
                
                if (idx == selected) {
                    attron(COLOR_PAIR(HIGHLIGHT_COLOR));
                    mvprintw(y, 0, " %s", files[idx].name.c_str());
                    clrtoeol();
                    
                    // 显示文件信息
                    attron(COLOR_PAIR(INFO_COLOR));
                    if (files[idx].is_directory) {
                        mvprintw(y, COLS-10, "<DIR>");
                    } else {
                        char size_str[16];
                        if (files[idx].size < 1024) {
                            snprintf(size_str, sizeof(size_str), "%5dB", (int)files[idx].size);
                        } else if (files[idx].size < 1024*1024) {
                            snprintf(size_str, sizeof(size_str), "%5.1fK", files[idx].size/1024.0);
                        } else {
                            snprintf(size_str, sizeof(size_str), "%5.1fM", files[idx].size/(1024.0*1024.0));
                        }
                        mvprintw(y, COLS-10, "%s", size_str);
                    }
                    attroff(COLOR_PAIR(INFO_COLOR));
                    attroff(COLOR_PAIR(HIGHLIGHT_COLOR));
                } else {
                    if (files[idx].is_directory) {
                        attron(A_BOLD);
                        mvprintw(y, 0, " %s/", files[idx].name.c_str());
                        attroff(A_BOLD);
                    } else {
                        mvprintw(y, 0, " %s", files[idx].name.c_str());
                    }
                    
                    // 非选中项也显示大小
                    attron(COLOR_PAIR(INFO_COLOR));
                    if (files[idx].is_directory) {
                        mvprintw(y, COLS-10, "<DIR>");
                    } else {
                        char size_str[16];
                        if (files[idx].size < 1024) {
                            snprintf(size_str, sizeof(size_str), "%5dB", (int)files[idx].size);
                        } else if (files[idx].size < 1024*1024) {
                            snprintf(size_str, sizeof(size_str), "%5.1fK", files[idx].size/1024.0);
                        } else {
                            snprintf(size_str, sizeof(size_str), "%5.1fM", files[idx].size/(1024.0*1024.0));
                        }
                        mvprintw(y, COLS-10, "%s", size_str);
                    }
                    attroff(COLOR_PAIR(INFO_COLOR));
                }
            }

            // 显示滚动提示
            mvprintw(LINES-1, 0, "文件数: %zu %d/%d", 
                     files.size(), selected+1, (int)files.size());
            clrtoeol();
            
            refresh();
            refresh_needed = false;
        }
        
        // 处理输入
        int ch = getch();
        if (ch == ERR) { // 没有输入
            napms(50); // 短暂休眠减少CPU占用
            continue;
        }
        
        switch (ch) {
            case KEY_UP:
                if (selected > 0) {
                    selected--;
                    refresh_needed = true;
                }
                if (selected < scroll_pos) {
                    scroll_pos = selected;
                }
                break;
            case KEY_DOWN:
                if (selected < (int)files.size() - 1) {
                    selected++;
                    refresh_needed = true;
                }
                if (selected >= scroll_pos + (LINES - 4)) {
                    scroll_pos = selected - (LINES - 4) + 1;
                }
                break;
            case KEY_PPAGE:
                if (selected > 0) {
                    selected = std::max(0, selected - (LINES - 4));
                    scroll_pos = std::max(0, scroll_pos - (LINES - 4));
                    refresh_needed = true;
                }
                break;
            case KEY_NPAGE:
                if (selected < (int)files.size() - 1) {
                    selected = std::min((int)files.size() - 1, selected + (LINES - 4));
                    scroll_pos = std::min((int)files.size() - (LINES - 4), scroll_pos + (LINES - 4));
                    refresh_needed = true;
                }
                break;
            case KEY_HOME:
                selected = 0;
                scroll_pos = 0;
                refresh_needed = true;
                break;
            case KEY_END:
                selected = files.size() - 1;
                scroll_pos = std::max(0, (int)files.size() - (LINES - 4));
                refresh_needed = true;
                break;
            case 10: // Enter
                if (files[selected].is_directory) {
                    if (files[selected].name == "..") {
                        fs::path parent_path = fs::path(current_dir).parent_path();
                        if (!parent_path.empty()) {
                            current_dir = parent_path.string();
                        } else {
                            current_dir = "/";
                        }
                    } else if (files[selected].name == ".") {
                        // 保持在当前目录
                    } else {
                        current_dir = (fs::path(current_dir) / files[selected].name).string();
                    }
                    selected = 0;
                    scroll_pos = 0;
                    files = read_directory(current_dir);
                    refresh_needed = true;
                } else {
                    std::string selected_file = (fs::path(current_dir) / files[selected].name).string();
                    endwin();
                    return selected_file;
                }
                break;
            case 27: // ESC
                endwin();
                return "";
            case 'r': // 手动刷新
            case 'R':
                // 清除缓存并重新加载
                dir_cache.erase(current_dir);
                files = read_directory(current_dir);
                refresh_needed = true;
                break;
            default:
                break;
        }
    }
}

// 显示错误消息
void show_error(const std::string& msg) {
    int width = std::max(40, (int)msg.length() + 4);
    int height = 5;
    int startx = (COLS - width) / 2;
    int starty = (LINES - height) / 2;

    WINDOW* win = newwin(height, width, starty, startx);
    box(win, 0, 0);
    
    wattron(win, COLOR_PAIR(ERROR_COLOR));
    mvwprintw(win, 1, (width - msg.length())/2, "%s", msg.c_str());
    wattroff(win, COLOR_PAIR(ERROR_COLOR));
    
    mvwprintw(win, 3, (width - 10)/2, "[ 确定 ]");
    wrefresh(win);
    
    wgetch(win);
    delwin(win);
}

// 用户选择器
std::string select_from_list(const std::vector<std::string>& items, const std::string& title) {
    int selected = 0;
    int scroll_pos = 0;
    int max_display = LINES - 4; // 调整显示区域大小

    // 初始化窗口
    WINDOW* win = newwin(LINES, COLS, 0, 0);
    keypad(win, TRUE);
    nodelay(win, FALSE); // 阻塞输入
    
    // 主循环
    bool refresh_needed = true;
    while (true) {
        if (refresh_needed) {
            wclear(win);
            
            // 显示标题
            wattron(win, COLOR_PAIR(TITLE_COLOR));
            mvwprintw(win, 0, 0, "%s", title.c_str());
            wclrtoeol(win);
            wattroff(win, COLOR_PAIR(TITLE_COLOR));
            
            // 显示项目列表
            int display_count = std::min(max_display, (int)items.size() - scroll_pos);
            for (int i = 0; i < display_count; i++) {
                int idx = i + scroll_pos;
                int y = i + 2;
                
                if (idx == selected) {
                    wattron(win, COLOR_PAIR(HIGHLIGHT_COLOR));
                    mvwprintw(win, y, 0, "> %s", items[idx].c_str());
                    wclrtoeol(win);
                    wattroff(win, COLOR_PAIR(HIGHLIGHT_COLOR));
                } else {
                    mvwprintw(win, y, 2, "%s", items[idx].c_str());
                }
            }
            
            // 显示提示
            mvwprintw(win, LINES-2, 0, "使用方向键选择，Enter确认，ESC取消");
            wclrtoeol(win);
            
            wrefresh(win);
            refresh_needed = false;
        }
        
        // 处理输入
        int ch = wgetch(win);
        switch (ch) {
            case KEY_UP:
                if (selected > 0) {
                    selected--;
                    refresh_needed = true;
                }
                if (selected < scroll_pos) scroll_pos = selected;
                break;
            case KEY_DOWN:
                if (selected < (int)items.size() - 1) {
                    selected++;
                    refresh_needed = true;
                }
                if (selected >= scroll_pos + max_display) scroll_pos = selected - max_display + 1;
                break;
            case KEY_PPAGE:
                selected = std::max(0, selected - max_display);
                scroll_pos = std::max(0, scroll_pos - max_display);
                refresh_needed = true;
                break;
            case KEY_NPAGE:
                selected = std::min((int)items.size() - 1, selected + max_display);
                scroll_pos = std::min((int)items.size() - max_display, scroll_pos + max_display);
                refresh_needed = true;
                break;
            case 10: // Enter
                delwin(win);
                return items[selected];
            case 27: // ESC
                delwin(win);
                return "";
            default:
                break;
        }
    }
}

// 获取所有系统用户
std::vector<std::string> get_all_users() {
    static std::vector<std::string> cached_users;
    if (!cached_users.empty()) return cached_users;
    
    // Termux 特殊处理
    #ifdef __ANDROID__
        // 在 Android/Termux 环境中，我们只能获取当前用户
        struct passwd* pwd = getpwuid(getuid());
        if (pwd) {
            cached_users.push_back(pwd->pw_name);
        }
        // 尝试添加 root 用户
        cached_users.push_back("root");
    #else
        // 标准 Linux 环境
        struct passwd* pwd;
        setpwent();
        while ((pwd = getpwent()) != nullptr) {
            cached_users.push_back(pwd->pw_name);
        }
        endpwent();
        std::sort(cached_users.begin(), cached_users.end());
    #endif
    
    return cached_users;
}

// 获取所有系统组
std::vector<std::string> get_all_groups() {
    static std::vector<std::string> cached_groups;
    if (!cached_groups.empty()) return cached_groups;
    
    // Termux 特殊处理
    #ifdef __ANDROID__
        // 在 Android/Termux 环境中，我们只能获取当前组
        struct group* grp = getgrgid(getgid());
        if (grp) {
            cached_groups.push_back(grp->gr_name);
        }
        // 尝试添加 root 组
        cached_groups.push_back("root");
    #else
        // 标准 Linux 环境
        struct group* grp;
        setgrent();
        while ((grp = getgrent()) != nullptr) {
            cached_groups.push_back(grp->gr_name);
        }
        endgrent();
        std::sort(cached_groups.begin(), cached_groups.end());
    #endif
    
    return cached_groups;
}

// 权限设置界面
void setup_interface(const std::string& filename) {
    // 获取文件信息
    struct stat file_stat;
    if (stat(filename.c_str(), &file_stat) != 0) {
        show_error("无法获取文件信息");
        return;
    }

    // 获取用户名和组名
    struct passwd* pwd = getpwuid(file_stat.st_uid);
    struct group* grp = getgrgid(file_stat.st_gid);
    std::string username = pwd ? pwd->pw_name : "未知用户";
    std::string groupname = grp ? grp->gr_name : "未知组";

    // 权限字符串
    char perm_str[11];
    mode_t mode = file_stat.st_mode;
    snprintf(perm_str, sizeof(perm_str), "%c%c%c%c%c%c%c%c%c%c",
        (S_ISDIR(mode)) ? 'd' : '-',
        (mode & S_IRUSR) ? 'r' : '-',
        (mode & S_IWUSR) ? 'w' : '-',
        (mode & S_IXUSR) ? 'x' : '-',
        (mode & S_IRGRP) ? 'r' : '-',
        (mode & S_IWGRP) ? 'w' : '-',
        (mode & S_IXGRP) ? 'x' : '-',
        (mode & S_IROTH) ? 'r' : '-',
        (mode & S_IWOTH) ? 'w' : '-',
        (mode & S_IXOTH) ? 'x' : '-');

    // 初始化界面
    initscr();
    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0); // 隐藏光标
    nodelay(stdscr, TRUE); // 非阻塞输入
    init_pair(HIGHLIGHT_COLOR, COLOR_BLACK, COLOR_WHITE);
    init_pair(TITLE_COLOR, COLOR_WHITE, COLOR_BLUE);
    init_pair(ERROR_COLOR, COLOR_WHITE, COLOR_RED);
    init_pair(CHECKBOX_COLOR, COLOR_GREEN, COLOR_BLACK);
    init_pair(INFO_COLOR, COLOR_CYAN, COLOR_BLACK);

    int selected = 0;
    bool apply_changes = false;
    bool refresh_needed = true;

    // 提取当前权限位
    bool user_read = mode & S_IRUSR;
    bool user_write = mode & S_IWUSR;
    bool user_exec = mode & S_IXUSR;
    bool group_read = mode & S_IRGRP;
    bool group_write = mode & S_IWGRP;
    bool group_exec = mode & S_IXGRP;
    bool other_read = mode & S_IROTH;
    bool other_write = mode & S_IWOTH;
    bool other_exec = mode & S_IXOTH;

    // 主循环
    while (true) {
        if (refresh_needed) {
            clear();
            attron(COLOR_PAIR(TITLE_COLOR));
            mvprintw(0, 0, "文件权限设置: %s", filename.c_str());
            clrtoeol();
            attroff(COLOR_PAIR(TITLE_COLOR));
            
            // 显示当前权限信息
            mvprintw(2, 2, "当前权限:");
            mvprintw(3, 4, "模式: %s (%04o)", perm_str, mode & 07777);
            mvprintw(4, 4, "用户: %s", username.c_str());
            mvprintw(5, 4, "组: %s", groupname.c_str());
            
            // 显示权限设置选项
            mvprintw(7, 2, "设置权限:");
            
            // 用户权限
            mvprintw(8, 4, "用户权限 (%s):", username.c_str());
            attron(selected == 0 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            mvprintw(9, 6, "[%c] 读取", user_read ? 'X' : ' ');
            attroff(selected == 0 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            
            attron(selected == 1 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            mvprintw(9, 20, "[%c] 写入", user_write ? 'X' : ' ');
            attroff(selected == 1 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            
            attron(selected == 2 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            mvprintw(9, 34, "[%c] 执行", user_exec ? 'X' : ' ');
            attroff(selected == 2 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            
            // 组权限
            mvprintw(10, 4, "组权限 (%s):", groupname.c_str());
            attron(selected == 3 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            mvprintw(11, 6, "[%c] 读取", group_read ? 'X' : ' ');
            attroff(selected == 3 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            
            attron(selected == 4 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            mvprintw(11, 20, "[%c] 写入", group_write ? 'X' : ' ');
            attroff(selected == 4 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            
            attron(selected == 5 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            mvprintw(11, 34, "[%c] 执行", group_exec ? 'X' : ' ');
            attroff(selected == 5 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            
            // 其他用户权限
            mvprintw(12, 4, "其他用户权限:");
            attron(selected == 6 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            mvprintw(13, 6, "[%c] 读取", other_read ? 'X' : ' ');
            attroff(selected == 6 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            
            attron(selected == 7 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            mvprintw(13, 20, "[%c] 写入", other_write ? 'X' : ' ');
            attroff(selected == 7 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            
            attron(selected == 8 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            mvprintw(13, 34, "[%c] 执行", other_exec ? 'X' : ' ');
            attroff(selected == 8 ? COLOR_PAIR(HIGHLIGHT_COLOR) : COLOR_PAIR(CHECKBOX_COLOR));
            
            // 用户和组选择
            attron(selected == 9 ? COLOR_PAIR(HIGHLIGHT_COLOR) : A_NORMAL);
            mvprintw(15, 4, "更改用户: %s", username.c_str());
            attroff(selected == 9 ? COLOR_PAIR(HIGHLIGHT_COLOR) : A_NORMAL);
            
            attron(selected == 10 ? COLOR_PAIR(HIGHLIGHT_COLOR) : A_NORMAL);
            mvprintw(16, 4, "更改组: %s", groupname.c_str());
            attroff(selected == 10 ? COLOR_PAIR(HIGHLIGHT_COLOR) : A_NORMAL);
            
            // 操作按钮
            attron(selected == 11 ? COLOR_PAIR(HIGHLIGHT_COLOR) : A_NORMAL);
            mvprintw(18, 4, "应用更改");
            attroff(selected == 11 ? COLOR_PAIR(HIGHLIGHT_COLOR) : A_NORMAL);
            
            attron(selected == 12 ? COLOR_PAIR(HIGHLIGHT_COLOR) : A_NORMAL);
            mvprintw(18, 20, "取消");
            attroff(selected == 12 ? COLOR_PAIR(HIGHLIGHT_COLOR) : A_NORMAL);
            
            // 显示导航帮助
            attron(COLOR_PAIR(INFO_COLOR));
            mvprintw(LINES-1, 0, "方向键导航，空格切换权限，Enter确认选择");
            clrtoeol();
            attroff(COLOR_PAIR(INFO_COLOR));
            
            refresh();
            refresh_needed = false;
        }
        
        // 处理输入
        int ch = getch();
        if (ch == ERR) { // 没有输入
            napms(50); // 短暂休眠减少CPU占用
            continue;
        }
        
        switch (ch) {
            case KEY_UP:
                if (selected > 0) {
                    // 修正焦点移动逻辑
                    if (selected >= 3 && selected <= 5) {
                        // 从组权限回到用户权限
                        selected = (selected == 3) ? 0 : (selected == 4) ? 1 : 2;
                    } else if (selected >= 6 && selected <= 8) {
                        // 从其他权限回到组权限
                        selected = (selected == 6) ? 3 : (selected == 7) ? 4 : 5;
                    } else {
                        selected--;
                    }
                    refresh_needed = true;
                }
                break;
            case KEY_DOWN:
                if (selected < 12) {
                    // 修正焦点移动逻辑
                    if (selected >= 0 && selected <= 2) {
                        // 从用户权限到组权限
                        selected = (selected == 0) ? 3 : (selected == 1) ? 4 : 5;
                    } else if (selected >= 3 && selected <= 5) {
                        // 从组权限到其他权限
                        selected = (selected == 3) ? 6 : (selected == 4) ? 7 : 8;
                    } else {
                        selected++;
                    }
                    refresh_needed = true;
                }
                break;
            case KEY_LEFT:
                if (selected > 0) {
                    selected--;
                    refresh_needed = true;
                }
                break;
            case KEY_RIGHT:
                if (selected < 12) {
                    selected++;
                    refresh_needed = true;
                }
                break;
            case ' ': // 空格键切换权限
                switch (selected) {
                    case 0: user_read = !user_read; break;
                    case 1: user_write = !user_write; break;
                    case 2: user_exec = !user_exec; break;
                    case 3: group_read = !group_read; break;
                    case 4: group_write = !group_write; break;
                    case 5: group_exec = !group_exec; break;
                    case 6: other_read = !other_read; break;
                    case 7: other_write = !other_write; break;
                    case 8: other_exec = !other_exec; break;
                }
                refresh_needed = true;
                break;
            case 10: // Enter
                if (selected == 9) {
                    // 更改用户
                    std::vector<std::string> users = get_all_users();
                    std::string new_user = select_from_list(users, "选择用户");
                    if (!new_user.empty()) {
                        username = new_user;
                        refresh_needed = true;
                    }
                } else if (selected == 10) {
                    // 更改组
                    std::vector<std::string> groups = get_all_groups();
                    std::string new_group = select_from_list(groups, "选择组");
                    if (!new_group.empty()) {
                        groupname = new_group;
                        refresh_needed = true;
                    }
                } else if (selected == 11) {
                    // 应用更改
                    apply_changes = true;
                    break;
                } else if (selected == 12) {
                    // 取消
                    endwin();
                    return;
                }
                break;
            case 27: // ESC
                endwin();
                return;
            default:
                break;
        }
        
        // 更新权限字符串
        mode_t new_mode = 0;
        if (user_read) new_mode |= S_IRUSR;
        if (user_write) new_mode |= S_IWUSR;
        if (user_exec) new_mode |= S_IXUSR;
        if (group_read) new_mode |= S_IRGRP;
        if (group_write) new_mode |= S_IWGRP;
        if (group_exec) new_mode |= S_IXGRP;
        if (other_read) new_mode |= S_IROTH;
        if (other_write) new_mode |= S_IWOTH;
        if (other_exec) new_mode |= S_IXOTH;
        
        snprintf(perm_str, sizeof(perm_str), "%c%c%c%c%c%c%c%c%c%c",
            (S_ISDIR(mode)) ? 'd' : '-',
            user_read ? 'r' : '-',
            user_write ? 'w' : '-',
            user_exec ? 'x' : '-',
            group_read ? 'r' : '-',
            group_write ? 'w' : '-',
            group_exec ? 'x' : '-',
            other_read ? 'r' : '-',
            other_write ? 'w' : '-',
            other_exec ? 'x' : '-');
        
        if (apply_changes) break;
    }
    
    endwin();
    
    // 应用更改
    bool success = true;
    std::string error_msg;
    
    // 计算新的权限模式
    mode_t new_mode = 0;
    if (user_read) new_mode |= S_IRUSR;
    if (user_write) new_mode |= S_IWUSR;
    if (user_exec) new_mode |= S_IXUSR;
    if (group_read) new_mode |= S_IRGRP;
    if (group_write) new_mode |= S_IWGRP;
    if (group_exec) new_mode |= S_IXGRP;
    if (other_read) new_mode |= S_IROTH;
    if (other_write) new_mode |= S_IWOTH;
    if (other_exec) new_mode |= S_IXOTH;
    
    // 更改权限
    if (chmod(filename.c_str(), new_mode) != 0) {
        success = false;
        error_msg = "更改权限失败";
    }
    
    // 更改用户和组
    if (success) {
        struct passwd* new_pwd = getpwnam(username.c_str());
        struct group* new_grp = getgrnam(groupname.c_str());
        
        if (!new_pwd) {
            success = false;
            error_msg = "用户不存在";
        } else if (!new_grp) {
            success = false;
            error_msg = "组不存在";
        } else if (chown(filename.c_str(), new_pwd->pw_uid, new_grp->gr_gid) != 0) {
            success = false;
            error_msg = "更改所有者失败";
        }
    }
    
    // 显示操作结果
    initscr();
    clear();
    
    if (success) {
        mvprintw(LINES/2, (COLS-20)/2, "更改成功应用!");
    } else {
        attron(COLOR_PAIR(ERROR_COLOR));
        mvprintw(LINES/2, (COLS-error_msg.length())/2, "%s", error_msg.c_str());
        attroff(COLOR_PAIR(ERROR_COLOR));
    }
    
    mvprintw(LINES/2+1, (COLS-20)/2, "按任意键继续...");
    refresh();
    getch();
    endwin();
}

int main(int argc, char* argv[]) {
    std::string filename;
    
    if (argc > 1) {
        filename = argv[1];
    } else {
        filename = file_selector();
        if (filename.empty()) {
            return 0;
        }
    }
    
    setup_interface(filename);
    return 0;
}