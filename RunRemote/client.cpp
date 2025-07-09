// g++ client.cpp -o client -pthread

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

const int PORT = 8976;
const int THREADS = 50;
const std::string TARGET_IP = "192.168.1."; // 修改为您的网段

std::atomic<bool> found(false);
std::mutex cout_mutex;

void scan_ip_range(const std::string& base_ip, int start, int end) {
    for (int i = start; i <= end && !found; i++) {
        std::string ip = base_ip + std::to_string(i);
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT);
        
        if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
            continue;
        }

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            continue;
        }

        // 设置非阻塞
        fcntl(sock, F_SETFL, O_NONBLOCK);
        
        connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
        
        fd_set set;
        FD_ZERO(&set);
        FD_SET(sock, &set);
        struct timeval timeout = {0, 100000}; // 100ms超时
        
        if (select(sock + 1, nullptr, &set, nullptr, &timeout) > 0) {
            int error = 0;
            socklen_t len = sizeof(error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);
            
            if (error == 0 && !found.exchange(true)) {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "Found server at: " << ip << std::endl;
                const char* msg = "RUN";
                send(sock, msg, strlen(msg), 0);
            }
        }
        close(sock);
    }
}

int main() {
    std::vector<std::thread> threads;
    int range_per_thread = 255 / THREADS;
    
    for (int i = 0; i < THREADS; i++) {
        int start = i * range_per_thread + 1;
        int end = (i == THREADS - 1) ? 254 : (i + 1) * range_per_thread;
        threads.emplace_back(scan_ip_range, TARGET_IP, start, end);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    if (!found) {
        std::cout << "Server not found in the network." << std::endl;
    }
    
    return 0;
}