#!/usr/bin/env python3
"""
===========================================
      CodeSync Assistant - 代码同步助手
===========================================
功能：解析剪贴板中的代码变更，应用替换并自动Git管理
作者：AI助手
版本：1.0
===========================================
"""
import subprocess
import sys
import re
import os
from datetime import datetime

def get_clipboard():
    """获取剪贴板内容"""
    try:
        result = subprocess.run(['termux-clipboard-get'], 
                              capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout
        else:
            print("错误：无法获取剪贴板内容")
            return None
    except Exception as e:
        print(f"获取剪贴板时发生错误：{e}")
        return None

def add_indentation(text, indent_level=4):
    """为文本添加缩进"""
    if not text:
        return text
    
    indent = ' ' * indent_level
    lines = text.split('\n')
    indented_lines = [f"{indent}{line}" if line.strip() else line for line in lines]
    return '\n'.join(indented_lines)

def remove_indentation(text, indent_level=4):
    """移除文本的缩进"""
    if not text:
        return text
    
    indent = ' ' * indent_level
    lines = text.split('\n')
    cleaned_lines = []
    
    for line in lines:
        if line.startswith(indent):
            cleaned_lines.append(line[indent_level:])
        else:
            cleaned_lines.append(line)
    
    return '\n'.join(cleaned_lines)

def advanced_parse(content):
    """高级解析，支持多种格式和自动缩进处理"""
    # 尝试正则表达式匹配
    patterns = {
        'filename': r'##\s*FileName:\s*(.+)',
        'original': r'##\s*A\s*```(?:\w+)?\s*([\s\S]*?)```',
        'modified': r'##\s*B\s*```(?:\w+)?\s*([\s\S]*?)```'
    }
    
    data = {}
    
    # 提取文件名
    filename_match = re.search(patterns['filename'], content)
    data['filename'] = filename_match.group(1).strip() if filename_match else 'unknown.py'
    
    # 提取原代码
    original_match = re.search(patterns['original'], content)
    original_code = original_match.group(1).strip() if original_match else ''
    
    # 提取修改后的代码
    modified_match = re.search(patterns['modified'], content)
    modified_code = modified_match.group(1).strip() if modified_match else ''
    
    # 如果A部分为空，尝试其他匹配方式
    if not original_code:
        print("未找到标准格式的A部分，尝试其他匹配方式...")
        
        # 尝试匹配没有代码块标记的内容
        alt_patterns = {
            'original_alt': r'##\s*A\s*\n([\s\S]*?)(?=##\s*B|\Z)',
            'modified_alt': r'##\s*B\s*\n([\s\S]*?)(?=##\s*|\Z)'
        }
        
        original_alt_match = re.search(alt_patterns['original_alt'], content)
        modified_alt_match = re.search(alt_patterns['modified_alt'], content)
        
        if original_alt_match and modified_alt_match:
            original_code = original_alt_match.group(1).strip()
            modified_code = modified_alt_match.group(1).strip()
            print("找到替代格式的代码块")
    
    # 自动缩进处理
    needs_indentation = False
    
    # 检查是否需要添加缩进
    if original_code and not any(line.startswith('    ') for line in original_code.split('\n') if line.strip()):
        print("检测到代码缺少缩进，自动添加4空格缩进...")
        needs_indentation = True
    
    if needs_indentation:
        original_code = add_indentation(original_code)
        modified_code = add_indentation(modified_code)
    
    data['original'] = original_code
    data['modified'] = modified_code
    data['needs_indentation'] = needs_indentation
    
    return data

def smart_align_code(original, modified):
    """
    智能对齐代码，尝试匹配A和B部分的结构
    """
    if not original or not modified:
        return original, modified
    
    orig_lines = original.split('\n')
    mod_lines = modified.split('\n')
    
    # 检查缩进级别
    def get_avg_indent(lines):
        indents = []
        for line in lines:
            if line.strip():
                indent = len(line) - len(line.lstrip())
                if indent > 0:
                    indents.append(indent)
        return sum(indents) // len(indents) if indents else 0
    
    orig_indent = get_avg_indent(orig_lines)
    mod_indent = get_avg_indent(mod_lines)
    
    # 如果缩进不一致，进行对齐
    if orig_indent != mod_indent and orig_indent > 0:
        print(f"检测到缩进不一致 (A:{orig_indent} vs B:{mod_indent})，进行对齐...")
        if orig_indent == 4:  # A部分使用4空格缩进
            mod_lines = [add_indentation(line) if line.strip() else line for line in mod_lines]
        else:
            # 统一使用A部分的缩进
            indent_char = ' ' * orig_indent
            mod_lines = [f"{indent_char}{line.lstrip()}" if line.strip() else line for line in mod_lines]
    
    return '\n'.join(orig_lines), '\n'.join(mod_lines)

def confirm_replacement(original, modified, filename):
    """显示代码差异并请求用户确认替换"""
    print(f"\n📄 文件: {filename}")
    print("=" * 60)
    print("原代码 (A):")
    print("-" * 30)
    print(original)
    print("\n修改后代码 (B):")
    print("-" * 30)
    print(modified)
    print("=" * 60)
    
    while True:
        response = input("\n确认替换? (y/n): ").strip().lower()
        if response in ('y', 'yes', '确认'):
            return True
        elif response in ('n', 'no', '取消'):
            return False
        else:
            print("请输入 y(是) 或 n(否)")

def check_and_create_git_branch():
    """检查Git分支，如果不是AICODE开头则创建新分支"""
    try:
        # 检查是否在Git仓库中
        result = subprocess.run(['git', 'rev-parse', '--is-inside-work-tree'],
                              capture_output=True, text=True)
        if result.returncode != 0:
            print("⚠️  当前目录不是Git仓库")
            return False
            
        # 获取当前分支
        result = subprocess.run(['git', 'branch', '--show-current'],
                              capture_output=True, text=True)
        current_branch = result.stdout.strip()
        
        # 检查分支名是否以AICODE开头
        if current_branch.startswith('AICODE'):
            print(f"✅ 当前已在AICODE分支: {current_branch}")
            return True
        else:
            print(f"当前分支: {current_branch}")
            # 创建新的AICODE分支
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            new_branch = f"AICODE_{timestamp}"
            
            print(f"创建新分支: {new_branch}")
            result = subprocess.run(['git', 'checkout', '-b', new_branch],
                                  capture_output=True, text=True)
            
            if result.returncode == 0:
                print(f"✅ 已切换到新分支: {new_branch}")
                return True
            else:
                print(f"❌ 创建分支失败: {result.stderr}")
                return False
                
    except Exception as e:
        print(f"❌ Git操作出错: {e}")
        return False

def apply_code_replacement(filename, original, modified):
    """应用代码替换"""
    try:
        # 检查文件是否存在
        if not os.path.exists(filename):
            print(f"❌ 文件不存在: {filename}")
            return False
            
        # 读取文件内容
        with open(filename, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # 查找并替换代码
        if original in content:
            new_content = content.replace(original, modified)
            
            # 写入文件
            with open(filename, 'w', encoding='utf-8') as f:
                f.write(new_content)
            
            print(f"✅ 代码替换完成: {filename}")
            return True
        else:
            print(f"❌ 未找到匹配的原代码")
            print("原代码内容:")
            print(original)
            return False
            
    except Exception as e:
        print(f"❌ 文件操作出错: {e}")
        return False

def git_commit():
    """执行Git提交"""
    try:
        # 获取提交信息
        commit_msg = input("\n请输入提交信息: ").strip()
        if not commit_msg:
            print("❌ 提交信息不能为空")
            return False
            
        # 添加所有更改
        subprocess.run(['git', 'add', '.'], check=True)
        
        # 提交
        result = subprocess.run(['git', 'commit', '-m', commit_msg],
                              capture_output=True, text=True)
        
        if result.returncode == 0:
            print("✅ 代码已提交")
            print(f"提交信息: {commit_msg}")
            return True
        else:
            print(f"❌ 提交失败: {result.stderr}")
            return False
            
    except Exception as e:
        print(f"❌ Git提交出错: {e}")
        return False

def format_output(data):
    """格式化输出为统一格式"""
    # 在输出前进行智能对齐
    original_aligned, modified_aligned = smart_align_code(data['original'], data['modified'])
    
    output = f"""# UseTool: CodeEdit
## FileName: {data['filename']}
## A
```
{original_aligned}
```
## B
```
{modified_aligned}
```"""
    return output

def main():
    print("📋 剪贴板内容处理器")
    print("正在获取剪贴板内容...")
    
    content = get_clipboard()
    if not content:
        sys.exit(1)
    
    print("正在解析内容...")
    
    data = advanced_parse(content)
    
    if not data['original'] and not data['modified']:
        print("❌ 错误：未找到有效的代码块")
        print("请确保内容包含：")
        print("1. ## FileName: 文件名")
        print("2. ## A 部分的原代码块")
        print("3. ## B 部分的修改后代码块")
        sys.exit(1)
    
    # 如果只有B部分没有A部分，提示用户
    if not data['original'] and data['modified']:
        print("⚠️  警告：只找到B部分（修改后代码），缺少A部分（原代码）")
        print("将使用空的A部分")
        data['original'] = "# 原代码缺失\n# 请手动补充原代码内容"
    
    # 格式化输出
    formatted = format_output(data)
    
    print("\n" + "✅ 格式化完成：" + "="*40)
    print(formatted)
    
    # 请求用户确认替换
    if not confirm_replacement(data['original'], data['modified'], data['filename']):
        print("❌ 用户取消替换")
        sys.exit(0)
    
    # 检查并创建Git分支
    if not check_and_create_git_branch():
        print("❌ Git分支处理失败")
        sys.exit(1)
    
    # 应用代码替换
    if not apply_code_replacement(data['filename'], data['original'], data['modified']):
        print("❌ 代码替换失败")
        sys.exit(1)
    
    # 执行Git提交
    if not git_commit():
        print("❌ Git提交失败")
        sys.exit(1)
    
    # 复制格式化结果到剪贴板（可选）
    copy_choice = input("\n是否将格式化结果复制到剪贴板? (y/N): ").strip().lower()
    if copy_choice in ('y', 'yes'):
        try:
            subprocess.run(['termux-clipboard-set'], 
                          input=formatted, text=True, check=True)
            print("📋 格式化结果已复制到剪贴板")
        except Exception as e:
            print(f"⚠️  复制到剪贴板失败：{e}")

if __name__ == "__main__":
    main()