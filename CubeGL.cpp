// g++ -o CubeGL CubeGL.cpp -lncurses

#include <ncurses.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <climits>

// 3D点结构
struct Point3D {
    float x, y, z;
    Point3D(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}
    
    // 向量减法
    Point3D operator-(const Point3D& p) const {
        return Point3D(x - p.x, y - p.y, z - p.z);
    }
    
    // 向量加法
    Point3D operator+(const Point3D& p) const {
        return Point3D(x + p.x, y + p.y, z + p.z);
    }
    
    // 标量乘法
    Point3D operator*(float scalar) const {
        return Point3D(x * scalar, y * scalar, z * scalar);
    }
    
    // 点积
    float dot(const Point3D& p) const {
        return x * p.x + y * p.y + z * p.z;
    }
    
    // 叉积
    Point3D cross(const Point3D& p) const {
        return Point3D(
            y * p.z - z * p.y,
            z * p.x - x * p.z,
            x * p.y - y * p.x
        );
    }
    
    // 向量长度
    float magnitude() const {
        return sqrt(x*x + y*y + z*z);
    }
    
    // 归一化
    Point3D normalize() const {
        float mag = magnitude();
        if (mag > 0) return *this * (1.0f / mag);
        return *this;
    }
};

// 屏幕点结构
struct Point2D {
    int x, y;
    Point2D(int x = 0, int y = 0) : x(x), y(y) {}
};

// 面结构
struct Face {
    std::vector<int> vertices;
    Point3D normal;
    Point3D center;
    float shade;
};

// 旋转矩阵
void rotateX(Point3D& p, float angle) {
    float y = p.y * cos(angle) - p.z * sin(angle);
    float z = p.y * sin(angle) + p.z * cos(angle);
    p.y = y;
    p.z = z;
}

void rotateY(Point3D& p, float angle) {
    float x = p.x * cos(angle) + p.z * sin(angle);
    float z = -p.x * sin(angle) + p.z * cos(angle);
    p.x = x;
    p.z = z;
}

void rotateZ(Point3D& p, float angle) {
    float x = p.x * cos(angle) - p.y * sin(angle);
    float y = p.x * sin(angle) + p.y * cos(angle);
    p.x = x;
    p.y = y;
}

// 将3D点投影到2D屏幕
Point2D project(const Point3D& p, int width, int height, float scale) {
    // 透视投影 - 放大立方体
    float factor = 500.0f / (500.0f - p.z);
    int x = static_cast<int>(p.x * factor * scale * 2.5f) + width / 2; // 增加2.5倍放大
    int y = static_cast<int>(-p.y * factor * scale * 2.5f) + height / 2;
    return Point2D(x, y);
}

// 计算面的法向量和中心点
void computeFaceNormalAndCenter(Face& face, const std::vector<Point3D>& vertices) {
    if (face.vertices.size() < 3) return;
    
    Point3D p1 = vertices[face.vertices[0]];
    Point3D p2 = vertices[face.vertices[1]];
    Point3D p3 = vertices[face.vertices[2]];
    
    Point3D v1 = p2 - p1;
    Point3D v2 = p3 - p1;
    face.normal = v1.cross(v2).normalize();
    
    // 计算中心点
    face.center = Point3D();
    for (int idx : face.vertices) {
        face.center = face.center + vertices[idx];
    }
    face.center = face.center * (1.0f / face.vertices.size());
}

// 计算光照强度 - 增强光影效果
float computeShade(const Point3D& lightDir, const Point3D& normal) {
    float intensity = normal.dot(lightDir);
    // 增加对比度，使光影更明显
    intensity = (intensity + 1.0f) / 2.0f; // 映射到0-1范围
    intensity = intensity * 0.8f + 0.2f; // 提高整体亮度
    return std::max(0.1f, std::min(1.0f, intensity)); // 限制在0.1-1.0范围
}

// 扫描线填充算法 - 确保面是实心的
void fillFace(WINDOW* win, const std::vector<Point2D>& points, char fillChar) {
    if (points.size() < 3) return;
    
    int max_y, max_x;
    getmaxyx(win, max_y, max_x);
    
    // 找到多边形的边界
    int minY = max_y, maxY = 0;
    int minX = max_x, maxX = 0;
    for (const auto& p : points) {
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
    }
    
    // 限制在屏幕范围内
    minY = std::max(0, minY);
    maxY = std::min(max_y - 1, maxY);
    minX = std::max(0, minX);
    maxX = std::min(max_x - 1, maxX);
    
    // 扫描线填充
    for (int y = minY; y <= maxY; y++) {
        std::vector<int> intersections;
        
        // 查找所有交点
        for (size_t i = 0; i < points.size(); i++) {
            size_t next = (i + 1) % points.size();
            int x1 = points[i].x, y1 = points[i].y;
            int x2 = points[next].x, y2 = points[next].y;
            
            if ((y1 <= y && y < y2) || (y2 <= y && y < y1)) {
                if (y1 != y2) { // 避免除以零
                    float t = static_cast<float>(y - y1) / (y2 - y1);
                    int x = static_cast<int>(x1 + t * (x2 - x1));
                    intersections.push_back(x);
                }
            }
        }
        
        // 排序交点
        std::sort(intersections.begin(), intersections.end());
        
        // 填充扫描线
        for (size_t i = 0; i < intersections.size(); i += 2) {
            if (i + 1 < intersections.size()) {
                int startX = std::max(minX, intersections[i]);
                int endX = std::min(maxX, intersections[i + 1]);
                
                // 确保起始位置小于结束位置
                if (startX > endX) std::swap(startX, endX);
                
                // 填充扫描线
                for (int x = startX; x <= endX; x++) {
                    mvwaddch(win, y, x, fillChar);
                }
            }
        }
    }
}

// 绘制带光照的立方体
void drawCube(WINDOW* win, const std::vector<Point3D>& vertices, 
             const std::vector<Face>& faces, const Point3D& lightDir, float scale) {
    int max_y, max_x;
    getmaxyx(win, max_y, max_x);
    
    // 按深度排序（画家算法）
    std::vector<std::pair<float, int>> depthOrder;
    for (int i = 0; i < faces.size(); i++) {
        depthOrder.push_back({faces[i].center.z, i});
    }
    std::sort(depthOrder.begin(), depthOrder.end(), 
             [](auto& a, auto& b) { return a.first > b.first; });
    
    // 绘制每个面
    for (auto& [depth, idx] : depthOrder) {
        const Face& face = faces[idx];
        
        // 获取面的所有2D投影点
        std::vector<Point2D> points;
        for (int vIdx : face.vertices) {
            points.push_back(project(vertices[vIdx], max_x, max_y, scale));
        }
        
        // 字符亮度映射 - 使用更明显的渐变
        const char shades[] = " .-:=+*#%@";
        int shadeIdx = static_cast<int>(face.shade * 9);
        char fillChar = shades[shadeIdx];
        
        // 使用扫描线填充算法填充面
        fillFace(win, points, fillChar);
        
        // 绘制边线
        for (size_t i = 0; i < points.size(); i++) {
            size_t next = (i + 1) % points.size();
            Point2D p1 = points[i];
            Point2D p2 = points[next];
            
            // Bresenham画线算法
            int dx = abs(p2.x - p1.x);
            int dy = abs(p2.y - p1.y);
            int sx = p1.x < p2.x ? 1 : -1;
            int sy = p1.y < p2.y ? 1 : -1;
            int err = dx - dy;
            
            Point2D current = p1;
            while (true) {
                if (current.x >= 0 && current.x < max_x && current.y >= 0 && current.y < max_y) {
                    mvwaddch(win, current.y, current.x, '#');
                }
                
                if (current.x == p2.x && current.y == p2.y) break;
                
                int e2 = 2 * err;
                if (e2 > -dy) {
                    err -= dy;
                    current.x += sx;
                }
                if (e2 < dx) {
                    err += dx;
                    current.y += sy;
                }
            }
        }
    }
}

int main() {
    // 初始化ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    nodelay(stdscr, TRUE); // 非阻塞输入
    
    // 设置颜色（如果终端支持）
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_WHITE, COLOR_BLACK);
        attron(COLOR_PAIR(1));
    }
    
    // 初始立方体大小（基于屏幕大小自适应）
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    float baseSize = std::min(max_x, max_y) * 0.25f; // 增加25%大小
    float scale = baseSize / 20.0f; // 缩放因子
    
    // 创建立方体顶点
    float size = 20.0f;
    std::vector<Point3D> vertices = {
        {-size, -size, -size}, { size, -size, -size}, { size,  size, -size}, {-size,  size, -size},
        {-size, -size,  size}, { size, -size,  size}, { size,  size,  size}, {-size,  size,  size}
    };
    
    // 创建立方体面 - 确保所有面都是四边形
    std::vector<Face> faces = {
        {{0, 1, 2, 3}}, // 背面
        {{4, 5, 6, 7}}, // 前面
        {{0, 1, 5, 4}}, // 底面
        {{2, 3, 7, 6}}, // 顶面
        {{0, 3, 7, 4}}, // 左面
        {{1, 2, 6, 5}}  // 右面
    };
    
    // 光源方向
    Point3D lightDir = {0.5f, -0.5f, -1.0f};
    lightDir = lightDir.normalize();
    
    // 旋转角度
    float angleX = 0.0f;
    float angleY = 0.0f;
    
    // 主循环
    bool running = true;
    while (running) {
        // 获取当前屏幕大小
        getmaxyx(stdscr, max_y, max_x);
        baseSize = std::min(max_x, max_y) * 0.25f; // 保持25%的屏幕大小
        scale = baseSize / 20.0f;
        
        clear();
        
        // 创建旋转后的顶点
        std::vector<Point3D> rotatedVertices = vertices;
        for (auto& v : rotatedVertices) {
            rotateX(v, angleX);
            rotateY(v, angleY);
        }
        
        // 更新面的法向量和光照 - 使用旋转后的顶点
        for (auto& face : faces) {
            computeFaceNormalAndCenter(face, rotatedVertices);
            face.shade = computeShade(lightDir, face.normal);
        }
        
        // 绘制立方体
        drawCube(stdscr, rotatedVertices, faces, lightDir, scale);
        
        // 显示控制信息
        mvprintw(0, 0, "Rotate: W/S/A/D | Light: Arrow Keys | Exit: Q");
        mvprintw(1, 0, "Rotation: X=%.2f Y=%.2f", angleX, angleY);
        mvprintw(2, 0, "Light: X=%.2f Y=%.2f Z=%.2f", 
                lightDir.x, lightDir.y, lightDir.z);
        mvprintw(3, 0, "Screen: %dx%d | Cube Size: %.1f", max_x, max_y, size);
        
        refresh();
        
        // 处理输入
        int ch = getch();
        switch (ch) {
            case 'q': case 'Q':
                running = false;
                break;
                
            // 旋转控制
            case 'w': case 'W':
                angleX += 0.1f;
                break;
            case 's': case 'S':
                angleX -= 0.1f;
                break;
            case 'a': case 'A':
                angleY += 0.1f;
                break;
            case 'd': case 'D':
                angleY -= 0.1f;
                break;
                
            // 光源控制
            case KEY_UP:
                lightDir.y += 0.1f;
                break;
            case KEY_DOWN:
                lightDir.y -= 0.1f;
                break;
            case KEY_LEFT:
                lightDir.x -= 0.1f;
                break;
            case KEY_RIGHT:
                lightDir.x += 0.1f;
                break;
            case '+': case '=':
                lightDir.z += 0.1f;
                break;
            case '-': case '_':
                lightDir.z -= 0.1f;
                break;
                
            // 调整立方体大小
            case '>': case '.':
                size *= 1.2f; // 增加20%
                vertices = {
                    {-size, -size, -size}, { size, -size, -size}, { size,  size, -size}, {-size,  size, -size},
                    {-size, -size,  size}, { size, -size,  size}, { size,  size,  size}, {-size,  size,  size}
                };
                break;
            case '<': case ',':
                size *= 0.8f; // 减小20%
                if (size < 5.0f) size = 5.0f; // 最小尺寸
                vertices = {
                    {-size, -size, -size}, { size, -size, -size}, { size,  size, -size}, {-size,  size, -size},
                    {-size, -size,  size}, { size, -size,  size}, { size,  size,  size}, {-size,  size,  size}
                };
                break;
        }
        
        // 归一化光源方向
        lightDir = lightDir.normalize();
        
        // 添加小延迟使动画更平滑
        usleep(30000);
    }
    
    endwin();
    return 0;
}