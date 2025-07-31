import argparse
import os
import sys
from PIL import Image
import re

def parse_color(color_str):
    """
    解析颜色字符串，支持多种格式：
    - 颜色名称（如 'black', 'white'）
    - 十六进制（如 '#RRGGBB' 或 '#RGB'）
    - RGB（如 '255,255,255'）
    - 'terminal' 表示使用终端默认背景
    """
    # 常见颜色名称映射
    color_names = {
        'black': (0, 0, 0),
        'white': (255, 255, 255),
        'red': (255, 0, 0),
        'green': (0, 255, 0),
        'blue': (0, 0, 255),
        'yellow': (255, 255, 0),
        'magenta': (255, 0, 255),
        'cyan': (0, 255, 255),
        'gray': (128, 128, 128),
        'grey': (128, 128, 128),
        'orange': (255, 165, 0),
        'purple': (128, 0, 128),
        'brown': (165, 42, 42),
        'pink': (255, 192, 203),
        'terminal': 'terminal'
    }
    
    # 转换为小写
    color_str = color_str.strip().lower()
    
    # 检查是否是颜色名称
    if color_str in color_names:
        return color_names[color_str]
    
    # 检查是否是十六进制格式
    if color_str.startswith('#'):
        hex_str = color_str[1:]
        
        # 处理缩写格式 #RGB
        if len(hex_str) == 3:
            return (
                int(hex_str[0]*2, 16),
                int(hex_str[1]*2, 16),
                int(hex_str[2]*2, 16)
            )
        
        # 处理完整格式 #RRGGBB
        if len(hex_str) == 6:
            return (
                int(hex_str[0:2], 16),
                int(hex_str[2:4], 16),
                int(hex_str[4:6], 16)
            )
    
    # 检查是否是RGB格式
    if re.match(r'^\d{1,3},\d{1,3},\d{1,3}$', color_str):
        r, g, b = map(int, color_str.split(','))
        if all(0 <= c <= 255 for c in (r, g, b)):
            return (r, g, b)
    
    # 无效格式返回黑色
    return (0, 0, 0)

def main():
    parser = argparse.ArgumentParser(
        description='在终端使用半块字符预览图片',
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument('-i', required=True, help='输入图片文件路径')
    parser.add_argument('-f', help='起始坐标，格式为x,y (默认: 0,0)')
    parser.add_argument('-t', help='结束坐标，格式为x,y (默认: 图片右下角)')
    parser.add_argument('-w', '--width', type=int, 
                        help='指定最大单行长度(列数)，默认使用终端宽度')
    parser.add_argument('-s', '--scale', type=float, default=1.0,
                        help='缩放因子，默认1.0')
    parser.add_argument('-b', '--background', default='black',
                        help='背景颜色（名称、十六进制或RGB格式），默认黑色\n'
                             '使用"terminal"表示终端默认背景色\n'
                             '示例: black, #FF0000, 255,0,0, terminal')
    
    # 添加使用示例
    parser.epilog = (
        "使用示例:\n"
        "  1. 显示整个图片: python script.py -i image.jpg\n"
        "  2. 显示指定区域: python script.py -i image.jpg -f 10,20 -t 100,80\n"
        "  3. 使用指定宽度: python script.py -i image.jpg -w 100\n"
        "  4. 显示从(50,50)到右下角: python script.py -i image.jpg -f 50,50\n"
        "  5. 缩放图片: python script.py -i image.jpg -s 0.5\n"
        "  6. 使用终端背景: python script.py -i image.jpg -b terminal"
    )
    
    args = parser.parse_args()

    try:
        # 解析背景色
        bg_color = parse_color(args.background)
        
        # 打开图片
        with Image.open(args.i) as img:
            width, height = img.size
            
            # 设置默认坐标
            start_x, start_y = 0, 0
            end_x, end_y = width - 1, height - 1
            
            # 解析起始坐标（如果提供）
            if args.f:
                try:
                    start_x, start_y = map(int, args.f.split(','))
                except ValueError:
                    print("错误：起始坐标格式不正确，请使用'x,y'格式")
                    sys.exit(1)
            
            # 解析结束坐标（如果提供）
            if args.t:
                try:
                    end_x, end_y = map(int, args.t.split(','))
                except ValueError:
                    print("错误：结束坐标格式不正确，请使用'x,y'格式")
                    sys.exit(1)
            
            # 验证坐标有效性
            if not (0 <= start_x < width):
                print(f"错误：起始X坐标({start_x})超出图片范围(0-{width-1})")
                sys.exit(1)
            if not (0 <= start_y < height):
                print(f"错误：起始Y坐标({start_y})超出图片范围(0-{height-1})")
                sys.exit(1)
            if not (0 <= end_x < width):
                print(f"错误：结束X坐标({end_x})超出图片范围(0-{width-1})")
                sys.exit(1)
            if not (0 <= end_y < height):
                print(f"错误：结束Y坐标({end_y})超出图片范围(0-{height-1})")
                sys.exit(1)
            if start_x > end_x:
                print(f"错误：起始X坐标({start_x})不能大于结束X坐标({end_x})")
                sys.exit(1)
            if start_y > end_y:
                print(f"错误：起始Y坐标({start_y})不能大于结束Y坐标({end_y})")
                sys.exit(1)
            
            # 获取终端宽度
            try:
                term_cols, _ = os.get_terminal_size()
            except OSError:
                term_cols = 80
            
            # 使用指定的宽度或终端宽度
            max_width = args.width if args.width else term_cols
            
            # 计算裁剪区域尺寸
            crop_width = end_x - start_x + 1
            crop_height = end_y - start_y + 1
            
            # 计算缩放比例
            if crop_width > 0 and crop_height > 0:
                # 应用用户指定的缩放因子
                scaled_width = max(1, int(crop_width * args.scale))
                scaled_height = max(1, int(crop_height * args.scale))
                
                # 计算宽高比
                aspect_ratio = scaled_height / scaled_width
                
                # 计算最佳显示宽度
                display_width = min(max_width, term_cols)
                
                # 计算高度（保持宽高比）
                display_height = int(display_width * aspect_ratio)
            else:
                display_width, display_height = 1, 1
            
            # 裁剪并缩放图像
            cropped = img.crop((start_x, start_y, end_x + 1, end_y + 1))
            
            # 使用高质量缩放算法
            resized = cropped.resize(
                (display_width, display_height), 
                resample=Image.Resampling.LANCZOS
            )
            
            # 转换为RGB模式（处理透明背景）
            if resized.mode != 'RGB':
                resized = resized.convert('RGB')
            
            # 在终端使用"▀"字符显示图像
            pixels = resized.load()
            for y in range(0, display_height, 2):  # 每次处理两行
                for x in range(display_width):
                    # 获取上部和下部像素
                    r_top, g_top, b_top = pixels[x, y]
                    
                    # 检查是否有下一行
                    if y + 1 < display_height:
                        r_bottom, g_bottom, b_bottom = pixels[x, y + 1]
                    else:
                        # 使用背景色填充最后一行下部
                        if bg_color == 'terminal':
                            # 使用终端默认背景色
                            r_bottom, g_bottom, b_bottom = 'default', 'default', 'default'
                        else:
                            r_bottom, g_bottom, b_bottom = bg_color
                    
                    # 使用ANSI转义序列设置前景色(上部)和背景色(下部)
                    top_color = f"\033[38;2;{r_top};{g_top};{b_top}m"
                    
                    if bg_color == 'terminal' and r_bottom == 'default':
                        # 使用终端默认背景色
                        bottom_color = "\033[49m"
                    else:
                        bottom_color = f"\033[48;2;{r_bottom};{g_bottom};{b_bottom}m"
                    
                    sys.stdout.write(
                        f"{top_color}{bottom_color}▀\033[0m"
                    )
                sys.stdout.write("\n")
            
            # 显示信息摘要
            bg_info = f"背景: {args.background}"
            if args.background.lower() == 'terminal':
                bg_info = "背景: 终端默认"
                
            info = (
                f"图片: {os.path.basename(args.i)} | 原始尺寸: {width}x{height} | "
                f"区域: ({start_x},{start_y})-({end_x},{end_y}) [{crop_width}x{crop_height}] | "
                f"预览尺寸: {display_width}x{display_height} | "
                f"缩放: {args.scale}x | "
                f"{bg_info}"
            )
            # 使用反白显示
            print("\033[47;30m" + info + "\033[0m")
    
    except FileNotFoundError:
        print(f"错误：文件'{args.i}'不存在")
        sys.exit(1)
    except Exception as e:
        print(f"处理图片时出错: {str(e)}")
        sys.exit(1)

if __name__ == "__main__":
    main()