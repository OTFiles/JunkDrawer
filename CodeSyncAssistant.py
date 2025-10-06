#!/usr/bin/env python3
"""
===========================================
      CodeSync Assistant - 代码同步助手
===========================================
功能：解析剪贴板中的代码变更，应用替换并自动Git管理
作者：AI助手
版本：2.2
更新：修正替换逻辑，先匹配后确认
===========================================
"""
import subprocess
import sys
import re
import os
from datetime import datetime
import glob

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

def find_file_in_project(filename):
    """在项目中查找文件，包括子目录"""
    # 首先检查当前目录
    if os.path.exists(filename):
        return filename
    
    # 在当前目录的子目录中查找
    for root, dirs, files in os.walk('.'):
        if filename in files:
            return os.path.join(root, filename)
    
    # 使用通配符查找
    matches = glob.glob(f"**/{filename}", recursive=True)
    if matches:
        return matches[0]
    
    return None

def parse_multiple_code_edits(content):
    """解析多个CodeEdit块"""
    # 分割不同的CodeEdit块
    blocks = re.split(r'# UseTool:\s*CodeEdit\s*', content)
    code_edits = []
    
    for block in blocks:
        if not block.strip():
            continue
            
        # 提取文件名
        filename_match = re.search(r'##\s*FileName:\s*(.+)', block)
        if not filename_match:
            continue
            
        filename = filename_match.group(1).strip()
        
        # 提取原代码和修改后代码
        original_match = re.search(r'##\s*A\s*```(?:\w+)?\s*([\s\S]*?)```', block)
        modified_match = re.search(r'##\s*B\s*```(?:\w+)?\s*([\s\S]*?)```', block)
        
        original_code = original_match.group(1).strip() if original_match else ''
        modified_code = modified_match.group(1).strip() if modified_match else ''
        
        if original_code or modified_code:
            code_edits.append({
                'filename': filename,
                'original': original_code,
                'modified': modified_code
            })
    
    return code_edits

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

def is_function_definition(code):
    """检查代码是否是函数定义"""
    lines = code.strip().split('\n')
    if not lines:
        return False
    
    first_line = lines[0].strip()
    
    # Python函数定义
    if first_line.startswith('def ') and first_line.endswith(':'):
        return True
    
    # C/C++/Java函数定义
    if re.match(r'^(?:[\w\s\*&]+\s+)?\w+\s*\([^)]*\)\s*(?:\{|$)', first_line):
        return True
    
    # 检查是否有明显的函数特征
    if any(keyword in first_line for keyword in ['function ', 'func ', '()', '{}']):
        return True
    
    return False

def extract_function_name(code):
    """提取函数名"""
    first_line = code.strip().split('\n')[0].strip()
    
    # Python函数
    if first_line.startswith('def '):
        match = re.match(r'def\s+(\w+)\s*\(', first_line)
        return match.group(1) if match else None
    
    # C/C++/Java函数
    match = re.match(r'^(?:[\w\s\*&]+\s+)?(\w+)\s*\([^)]*\)', first_line)
    return match.group(1) if match else None

def fuzzy_search_function(filepath, original_code):
    """模糊搜索函数"""
    if not is_function_definition(original_code):
        print("❌ 原代码不是函数定义，无法进行模糊搜索")
        return None
    
    function_name = extract_function_name(original_code)
    if not function_name:
        print("❌ 无法提取函数名")
        return None
    
    print(f"🔍 正在模糊搜索函数: {function_name}")
    
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # 根据文件类型使用不同的正则表达式
        file_ext = os.path.splitext(filepath)[1].lower()
        
        if file_ext in ['.py']:
            # Python函数模式
            pattern = rf'def\s+{function_name}\s*\([^)]*\)\s*:[\s\S]*?(?=\n\S|\Z)'
        elif file_ext in ['.c', '.cpp', '.cc', '.cxx', '.h', '.hpp']:
            # C/C++函数模式
            pattern = rf'(?:[\w\s\*&]+\s+)?{function_name}\s*\([^)]*\)\s*{{[\s\S]*?(?=^\s*\w|\Z)}'
        elif file_ext in ['.java']:
            # Java函数模式
            pattern = rf'(?:[\w\s]+\s+)?{function_name}\s*\([^)]*\)\s*(?:throws\s+[\w\s,]+)?\s*{{[\s\S]*?(?=^\s*\w|\Z)}'
        else:
            # 通用函数模式
            pattern = rf'{function_name}\s*\([^)]*\)\s*{{[\s\S]*?(?=^\s*\w|\Z)}'
        
        matches = list(re.finditer(pattern, content, re.MULTILINE))
        
        if not matches:
            print(f"❌ 未找到函数 '{function_name}'")
            return None
        
        if len(matches) == 1:
            return matches[0].group(0)
        else:
            print(f"找到 {len(matches)} 个匹配的函数:")
            for i, match in enumerate(matches):
                print(f"{i+1}. {match.group(0)[:100]}...")
            
            while True:
                try:
                    choice = input("请选择要替换的函数 (输入数字或 'c' 取消): ").strip()
                    if choice.lower() == 'c':
                        return None
                    index = int(choice) - 1
                    if 0 <= index < len(matches):
                        return matches[index].group(0)
                    else:
                        print("无效的选择")
                except ValueError:
                    print("请输入数字")
    
    except Exception as e:
        print(f"❌ 模糊搜索出错: {e}")
        return None

def confirm_replacement(original, modified, filename, filepath):
    """显示代码差异并请求用户确认替换"""
    print(f"\n📄 文件: {filename} ({filepath})")
    print("=" * 60)
    print("原代码 (A):")
    print("-" * 30)
    print(original if original else "(空)")
    print("\n修改后代码 (B):")
    print("-" * 30)
    print(modified if modified else "(空)")
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

def check_exact_match(filepath, original_code):
    """检查文件中是否存在精确匹配的原代码"""
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        
        return original_code in content
    except Exception as e:
        print(f"❌ 检查精确匹配时出错: {e}")
        return False

def apply_code_replacement(filepath, original, modified):
    """应用代码替换"""
    try:
        # 检查文件是否存在
        if not os.path.exists(filepath):
            print(f"❌ 文件不存在: {filepath}")
            return False
            
        # 读取文件内容
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # 查找并替换代码
        if original in content:
            new_content = content.replace(original, modified)
            
            # 写入文件
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(new_content)
            
            print(f"✅ 代码替换完成: {filepath}")
            return True
        else:
            print(f"❌ 未找到匹配的原代码")
            return False
            
    except Exception as e:
        print(f"❌ 文件操作出错: {e}")
        return False

def fuzzy_apply_replacement(filepath, original_code, modified_code, original_function):
    """应用模糊替换"""
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # 替换整个函数
        if original_function in content:
            new_content = content.replace(original_function, modified_code)
            
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(new_content)
            
            print(f"✅ 函数替换完成: {filepath}")
            return True
        else:
            print("❌ 模糊替换失败")
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

def format_output(code_edits):
    """格式化输出为统一格式"""
    output = ""
    for i, edit in enumerate(code_edits):
        # 在输出前进行智能对齐
        original_aligned, modified_aligned = smart_align_code(
            edit['original'], edit['modified']
        )
        
        block_output = f"""# UseTool: CodeEdit
## FileName: {edit['filename']}
## A
```
{original_aligned}
```
## B
```
{modified_aligned}
```"""
        if i > 0:
            output += "\n\n"
        output += block_output
    
    return output

def process_single_code_edit(edit):
    """处理单个代码编辑块"""
    # 查找文件
    filepath = find_file_in_project(edit['filename'])
    if not filepath:
        print(f"❌ 找不到文件: {edit['filename']}")
        print("在以下位置查找:")
        print("  - 当前目录")
        print("  - 所有子目录")
        return None
    
    original_code = edit['original']
    modified_code = edit['modified']
    
    # 1. 先检查是否存在完全匹配的A
    print("🔍 检查精确匹配...")
    exact_match = check_exact_match(filepath, original_code)
    
    if exact_match:
        print("✅ 找到精确匹配")
        # 直接使用原代码和修改后代码
        final_original = original_code
        final_modified = modified_code
        match_type = "exact"
    else:
        print("❌ 精确匹配失败")
        
        # 2. 如果不存在且A是函数，进行函数模糊搜索
        if is_function_definition(original_code):
            print("🔍 检测到函数定义，尝试模糊搜索...")
            found_function = fuzzy_search_function(filepath, original_code)
            
            if found_function:
                print("✅ 模糊搜索找到匹配")
                final_original = found_function
                final_modified = modified_code
                match_type = "fuzzy"
            else:
                # 3. 如果模糊搜索失败，尝试添加缩进
                print("🔍 模糊搜索失败，尝试添加缩进...")
                indented_original = add_indentation(original_code)
                indented_modified = add_indentation(modified_code)
                
                if check_exact_match(filepath, indented_original):
                    print("✅ 添加缩进后找到匹配")
                    final_original = indented_original
                    final_modified = indented_modified
                    match_type = "indented"
                else:
                    print("❌ 所有匹配方法都失败")
                    return None
        else:
            # 4. 如果A不是函数，直接尝试添加缩进
            print("🔍 尝试添加缩进...")
            indented_original = add_indentation(original_code)
            indented_modified = add_indentation(modified_code)
            
            if check_exact_match(filepath, indented_original):
                print("✅ 添加缩进后找到匹配")
                final_original = indented_original
                final_modified = indented_modified
                match_type = "indented"
            else:
                print("❌ 所有匹配方法都失败")
                return None
    
    # 成功匹配后让用户确认替换
    if not confirm_replacement(final_original, final_modified, edit['filename'], filepath):
        print("❌ 用户取消替换")
        return None
    
    # 应用代码替换
    if match_type == "fuzzy":
        success = fuzzy_apply_replacement(filepath, original_code, final_modified, final_original)
    else:
        success = apply_code_replacement(filepath, final_original, final_modified)
    
    if success:
        # 返回成功处理的编辑信息
        return {
            'filename': edit['filename'],
            'original': final_original,
            'modified': final_modified,
            'match_type': match_type
        }
    else:
        print("❌ 代码替换失败")
        return None

def process_code_edits(code_edits):
    """处理多个代码编辑块"""
    successful_edits = []
    
    for i, edit in enumerate(code_edits):
        print(f"\n🔄 处理第 {i+1}/{len(code_edits)} 个代码编辑块...")
        
        result = process_single_code_edit(edit)
        if result:
            successful_edits.append(result)
    
    return successful_edits

def main():
    print("📋 CodeSync Assistant - 代码同步助手")
    print("正在获取剪贴板内容...")
    
    content = get_clipboard()
    if not content:
        sys.exit(1)
    
    print("正在解析内容...")
    
    # 解析多个CodeEdit块
    code_edits = parse_multiple_code_edits(content)
    
    if not code_edits:
        print("❌ 错误：未找到有效的CodeEdit块")
        print("请确保内容包含：")
        print("1. # UseTool: CodeEdit")
        print("2. ## FileName: 文件名")
        print("3. ## A 部分的原代码块")
        print("4. ## B 部分的修改后代码块")
        sys.exit(1)
    
    print(f"✅ 找到 {len(code_edits)} 个代码编辑块")
    
    # 检查并创建Git分支
    if not check_and_create_git_branch():
        print("❌ Git分支处理失败")
        sys.exit(1)
    
    # 处理所有代码编辑块
    successful_edits = process_code_edits(code_edits)
    
    if not successful_edits:
        print("❌ 没有成功应用的代码编辑")
        sys.exit(1)
    
    # 执行Git提交
    if not git_commit():
        print("❌ Git提交失败")
        sys.exit(1)
    
    # 复制格式化结果到剪贴板（可选）
    copy_choice = input("\n是否将格式化结果复制到剪贴板? (y/N): ").strip().lower()
    if copy_choice in ('y', 'yes'):
        try:
            formatted = format_output(successful_edits)
            subprocess.run(['termux-clipboard-set'], 
                          input=formatted, text=True, check=True)
            print("📋 格式化结果已复制到剪贴板")
        except Exception as e:
            print(f"⚠️  复制到剪贴板失败：{e}")

if __name__ == "__main__":
    main()