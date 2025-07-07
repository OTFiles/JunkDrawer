# 基于 baidu_translate_api 项目实现的较为完整功能的小程序

import baidu_translate as fanyi
import argparse
import subprocess
import sys
import time
import math
import os

def safe_translate(text, max_chunk_size=5000):
    """
    安全翻译长文本，自动分块处理
    max_chunk_size: 分块大小（字节数），留1000字节安全余量
    """
    # 计算文本的UTF-8编码字节长度
    byte_length = len(text.encode('utf-8'))
    
    # 如果文本在安全范围内，直接翻译
    if byte_length <= max_chunk_size:
        return fanyi.translate_text(text)
    
    # 长文本分块处理
    print(f"注意: 文本过长({byte_length}字节)，将分块翻译...")
    chunks = []
    current_chunk = ""
    current_size = 0
    
    # 按段落分割文本
    paragraphs = text.split('\n')
    
    for para in paragraphs:
        # 计算当前段落字节大小
        para_size = len(para.encode('utf-8'))
        
        # 如果段落本身超过最大块大小
        if para_size > max_chunk_size:
            # 按句子分割处理超大段落
            sentences = para.split('. ')
            for sent in sentences:
                sent_size = len(sent.encode('utf-8'))
                if sent_size > max_chunk_size:
                    # 极端情况：单句超过限制，强制分割
                    num_chunks = math.ceil(sent_size / max_chunk_size)
                    chunk_size = len(sent) // num_chunks
                    for i in range(0, len(sent), chunk_size):
                        sub_chunk = sent[i:i+chunk_size]
                        chunks.append(fanyi.translate_text(sub_chunk))
                else:
                    if current_size + sent_size > max_chunk_size:
                        chunks.append(fanyi.translate_text(current_chunk))
                        current_chunk = sent
                        current_size = sent_size
                    else:
                        current_chunk += sent + ". "
                        current_size += sent_size + 2
        else:
            if current_size + para_size > max_chunk_size:
                chunks.append(fanyi.translate_text(current_chunk))
                current_chunk = para + "\n"
                current_size = para_size + 1
            else:
                current_chunk += para + "\n"
                current_size += para_size + 1
    
    # 添加最后一个块
    if current_chunk:
        chunks.append(fanyi.translate_text(current_chunk))
    
    # 合并翻译结果
    return "\n".join(chunks)

def main():
    parser = argparse.ArgumentParser(description='智能翻译工具')
    parser.add_argument('-t', '--text', nargs='+', help='翻译指定文本')
    parser.add_argument('-r', '--run', help='执行命令并实时翻译输出')
    parser.add_argument('-ra', '--run-all', help='执行命令后整体翻译输出')
    parser.add_argument('-f', '--file', help='翻译指定文件内容')
    parser.add_argument('-o', '--output', help='指定输出文件')
    
    args = parser.parse_args()
    
    # 检查参数冲突
    active_params = [args.text, args.run, args.run_all, args.file]
    if sum([1 for x in active_params if x]) > 1:
        print("错误: 参数 -t, -r, -ra, -f 不能同时使用")
        sys.exit(1)
    
    # 初始化输出文件
    output_file = None
    if args.output:
        try:
            output_file = open(args.output, 'w', encoding='utf-8')
            print(f"输出将保存到: {args.output}")
        except Exception as e:
            print(f"无法打开输出文件: {str(e)}")
            sys.exit(1)
    
    # 输出函数（控制台和文件）
    def write_output(content, file_only=False):
        """写入输出到控制台和/或文件"""
        if not file_only:
            print(content)
        if output_file:
            output_file.write(content + "\n")
    
    # 文本翻译模式
    if args.text:
        text = ' '.join(args.text)
        result = safe_translate(text)
        
        # 输出结果
        output_content = f"翻译结果: {result}"
        write_output(output_content)
    
    # 命令实时翻译模式
    elif args.run:
        CHUNK_SIZE = 5
        buffer = []
        last_translate_time = time.time()
        
        process = subprocess.Popen(
            args.run,
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1
        )
        
        def process_buffer(force=False):
            nonlocal buffer, last_translate_time
            current_time = time.time()
            if force or len(buffer) >= CHUNK_SIZE or (buffer and current_time - last_translate_time > 0.5):
                chunk = '\n'.join(buffer)
                if chunk.strip():
                    try:
                        translated = safe_translate(chunk)
                        # 输出翻译结果
                        write_output(f"\n[翻译] {translated}\n")
                    except Exception as e:
                        write_output(f"\n翻译出错: {str(e)}\n")
                buffer = []
                last_translate_time = current_time
        
        try:
            for line in iter(process.stdout.readline, ''):
                # 实时输出原始内容
                sys.stdout.write(line)
                sys.stdout.flush()
                if output_file:
                    output_file.write(line)
                
                buffer.append(line.strip())
                process_buffer()
            
            process_buffer(force=True)
        
        except KeyboardInterrupt:
            write_output("\n程序被中断")
            process.terminate()
        
        return_code = process.wait()
        if return_code != 0:
            write_output(f"命令执行失败，退出码: {return_code}")
        else:
            write_output("命令执行成功")
    
    # 命令整体翻译模式
    elif args.run_all:
        process = subprocess.Popen(
            args.run_all,
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True
        )
        
        output, _ = process.communicate()
        print_output(output, "命令输出", write_output)
        
        return_code = process.returncode
        if return_code != 0:
            write_output(f"\n命令执行失败，退出码: {return_code}")
        else:
            write_output("\n命令执行成功")
    
    # 文件翻译模式
    elif args.file:
        file_path = args.file
        
        # 检查文件是否存在
        if not os.path.isfile(file_path):
            write_output(f"错误: 文件 '{file_path}' 不存在")
            sys.exit(1)
        
        try:
            # 读取文件内容
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            # 输出原文和译文
            print_output(content, f"文件内容: {file_path}", write_output)
            
        except UnicodeDecodeError:
            # 尝试二进制读取
            try:
                with open(file_path, 'rb') as f:
                    content = f.read().decode('utf-8', errors='ignore')
                print_output(content, f"文件内容: {file_path}", write_output)
            except Exception as e:
                write_output(f"读取文件失败: {str(e)}")
                sys.exit(1)
        except Exception as e:
            write_output(f"读取文件失败: {str(e)}")
            sys.exit(1)
    
    # 关闭输出文件
    if output_file:
        output_file.close()
        print(f"输出已保存到: {args.output}")

def print_output(content, title, write_func):
    """通用输出函数，用于显示原文和翻译结果"""
    # 输出原始内容
    separator = "=" * 50
    write_func(separator)
    write_func(f"[{title}]")
    write_func(content.strip())
    write_func(separator)
    
    # 翻译并输出结果
    if content.strip():
        try:
            translated = safe_translate(content)
            write_func("\n" + separator)
            write_func("[翻译结果]")
            write_func(translated)
            write_func(separator)
        except Exception as e:
            write_func(f"\n翻译失败: {str(e)}")

if __name__ == "__main__":
    main()
