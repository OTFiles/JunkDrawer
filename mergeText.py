import os
import re

input_dir = 'in'  # 输入目录
output_file = 'merged.txt'  # 输出文件

# 检查输入目录是否存在
if not os.path.exists(input_dir):
    print(f"错误：输入目录 '{input_dir}' 不存在")
    exit()

if not os.path.isdir(input_dir):
    print(f"错误：'{input_dir}' 不是有效目录")
    exit()

# 获取输入目录文件列表
try:
    files = os.listdir(input_dir)
except PermissionError:
    print(f"错误：没有权限访问目录 '{input_dir}'")
    exit()

# 筛选四位数字命名的txt文件
pattern = re.compile(r'^\d{4}\.txt$')
matching_files = [
    f for f in files 
    if pattern.match(f) and os.path.isfile(os.path.join(input_dir, f))
]

# 检查是否找到有效文件
if not matching_files:
    print(f"在目录 '{input_dir}' 中未找到符合0001.txt格式的文件")
    exit()

# 按数字顺序排序文件
matching_files.sort(key=lambda x: int(x.split('.')[0]))

# 执行合并操作
try:
    with open(output_file, 'w', encoding='utf-8') as outfile:
        for filename in matching_files:
            filepath = os.path.join(input_dir, filename)
            with open(filepath, 'r', encoding='utf-8') as infile:
                outfile.write(infile.read())
                outfile.write('\n')  # 添加文件分隔换行
    print(f"成功合并 {len(matching_files)} 个文件 -> {output_file}")
except Exception as e:
    print(f"合并过程中出现错误: {str(e)}")
    if os.path.exists(output_file):
        os.remove(output_file)
    exit()
