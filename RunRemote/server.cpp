// g++ server.cpp -o server -pthread

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <thread>
#include <chrono>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

const int PORT = 8976;
const std::string SCRIPT = "./run.sh";

// 执行脚本函数
void execute_script() {
    std::cout << "Executing script: " << SCRIPT << std::endl;
    system(SCRIPT.c_str());
}

// 定时任务线程函数
void scheduler() {
/*
    while (true) {
        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm* local_time = std::localtime(&now_time);
        
        // 计算到下一个8:00的时间差
        int seconds_until_8am = 0;
        if (local_time->tm_hour < 8) {
            seconds_until_8am = (8 - local_time->tm_hour) * 3600 
                              - local_time->tm_min * 60 
                              - local_time->tm_sec;
        } else {
            seconds_until_8am = (24 - local_time->tm_hour + 8) * 3600 
                              - local_time->tm_min * 60 
                              - local_time->tm_sec;
        }
        
        // 休眠到8:00
        std::this_thread::sleep_for(std::chrono::seconds(seconds_until_8am));
        
        // 执行脚本
        execute_script();
        
        // 休眠1小时防止重复执行
        std::this_thread::sleep_for(std::chrono::hours(1));
    }
*/
}

// 处理客户端连接
void handle_client(int client_socket) {
    char buffer[1024] = {0};
    recv(client_socket, buffer, sizeof(buffer), 0);
    execute_script();
    close(client_socket);
}

int main() {
    // 启动定时任务线程
    std::thread scheduler_thread(scheduler);
    scheduler_thread.detach();

    // 创建Socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 设置端口复用
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 绑定地址和端口
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    // 开始监听
    if (listen(server_fd, 5) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "Server started on port " << PORT << std::endl;
    std::cout << "Waiting for connections..." << std::endl;

    // 主循环处理连接
    while (true) {
        int client_socket = accept(server_fd, nullptr, nullptr);
        if (client_socket < 0) {
            perror("accept");
            continue;
        }
        std::thread(handle_client, client_socket).detach();
    }

    close(server_fd);
    return 0;
}