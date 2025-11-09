import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from datetime import datetime, time
import re
from typing import List, Tuple, Dict, Optional
from dataclasses import dataclass
import argparse
import sys
import os
from matplotlib import font_manager
from fontTools.ttLib import TTFont
import warnings
import logging

# 配置日志
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger('FontFinder')

def has_chinese_chars(font_path):
    """检查字体是否包含基本中文字符"""
    try:
        # 处理字体集合文件(.ttc)
        if font_path.lower().endswith('.ttc'):
            return any(_check_ttc_subfont(font_path, i) for i in range(10))
        
        # 处理普通字体文件
        font = TTFont(font_path)
        for table in font['cmap'].tables:
            if table.isUnicode():
                # 检查是否包含中文常见字符（Unicode范围：4E00-9FFF）
                for code in range(0x4E00, 0x4E10):  # 只检查前16个基本汉字提高效率
                    if table.cmap.get(code):
                        return True
        return False
    except Exception as e:
        logger.warning(f"解析字体失败: {font_path} - {str(e)}")
        return False

def _check_ttc_subfont(ttc_path, index):
    """检查.ttc字体集合中的单个子字体"""
    try:
        font = TTFont(ttc_path, fontNumber=index)
        for table in font['cmap'].tables:
            if table.isUnicode():
                # 检查基本汉字
                for code in range(0x4E00, 0x4E10):
                    if table.cmap.get(code):
                        return True
        return False
    except:
        return False  # 忽略索引超出范围的情况

def find_chinese_font():
    """查找并设置中文字体"""
    # 获取系统所有字体路径
    font_paths = font_manager.findSystemFonts()
    
    chinese_fonts = []
    for path in font_paths:
        if has_chinese_chars(path):
            chinese_fonts.append(os.path.abspath(path))
    
    # 优先选择常见的中文字体
    preferred_fonts = [
        'SimHei', 'Microsoft YaHei', 'SimSun', 'KaiTi', 'FangSong',
        'STHeiti', 'STKaiti', 'STSong', 'STFangsong',
        'PingFang SC', 'Hiragino Sans GB', 'Heiti SC', 'WenQuanYi Micro Hei'
    ]
    
    # 首先尝试首选字体
    for font_name in preferred_fonts:
        try:
            font_path = font_manager.findfont(font_name)
            if font_path and has_chinese_chars(font_path):
                plt.rcParams['font.family'] = font_name
                print(f"使用中文字体: {font_name}")
                return True
        except:
            continue
    
    # 如果首选字体找不到，使用检测到的第一个中文字体
    if chinese_fonts:
        try:
            font_path = chinese_fonts[0]
            font_prop = font_manager.FontProperties(fname=font_path)
            font_name = font_prop.get_name()
            plt.rcParams['font.family'] = font_name
            print(f"使用检测到的中文字体: {font_name}")
            return True
        except:
            pass
    
    # 如果都找不到，使用默认字体
    print("警告: 未找到合适的中文字体，可能无法正确显示中文")
    return False

@dataclass
class InspectionRecord:
    """数据记录类，表示单次巡查记录"""
    teacher: str
    time: time
    
    @classmethod
    def from_string(cls, line: str):
        """从字符串解析巡查记录 - 更健壮的解析方法"""
        line = line.strip()
        
        # 更灵活的正则表达式，处理各种格式
        match = re.match(r'^(.+?)\s+(\d{1,2}:\d{2})$', line)
        if not match:
            # 尝试其他可能的格式
            match = re.match(r'^(.+?)\s+(\d{1,2}:\d{2}:\d{2})$', line)
            if not match:
                raise ValueError(f"无法解析的数据格式: {line}")
        
        teacher_str = match.group(1).strip()
        time_str = match.group(2)
        
        # 处理老师名称 - 更灵活的处理
        teacher = "unknown"
        if teacher_str.lower() not in ['unknown', 'unknow', '未知']:
            teacher = teacher_str.replace("'", "")  # 移除单引号
        
        # 解析时间 - 更安全的解析
        try:
            # 处理 HH:MM 格式
            if len(time_str.split(':')) == 2:
                time_obj = datetime.strptime(time_str, '%H:%M').time()
            # 处理 HH:MM:SS 格式
            else:
                time_obj = datetime.strptime(time_str, '%H:%M:%S').time()
        except ValueError as e:
            raise ValueError(f"无效的时间格式 '{time_str}': {e}")
        
        # 验证时间范围
        if time_obj.hour >= 24:
            raise ValueError(f"小时数不能超过23: {time_str}")
        
        return cls(teacher, time_obj)

class InspectionDataProcessor:
    """巡查数据处理类"""
    
    def __init__(self, segment_minutes: int = 10, start_time: str = "18:00", end_time: str = "21:00"):
        self.segment_minutes = segment_minutes
        self.start_time = self._parse_time_string(start_time)
        self.end_time = self._parse_time_string(end_time)
        self.records: List[InspectionRecord] = []
        
    def _parse_time_string(self, time_str: str) -> time:
        """解析时间字符串为time对象"""
        try:
            if len(time_str.split(':')) == 2:
                return datetime.strptime(time_str, '%H:%M').time()
            else:
                return datetime.strptime(time_str, '%H:%M:%S').time()
        except ValueError:
            raise ValueError(f"无效的时间格式: {time_str}")
    
    def load_data(self, file_path: str):
        """从文件加载数据"""
        self.records = []
        success_count = 0
        error_count = 0
        
        try:
            with open(file_path, 'r', encoding='utf-8') as file:
                for line_num, line in enumerate(file, 1):
                    line = line.strip()
                    if not line:  # 跳过空行
                        continue
                    try:
                        record = InspectionRecord.from_string(line)
                        # 检查记录时间是否在分析范围内
                        if self._is_time_in_range(record.time):
                            self.records.append(record)
                            success_count += 1
                        else:
                            print(f"信息: 第{line_num}行时间不在分析范围内: {record.time}")
                    except ValueError as e:
                        print(f"警告: 第{line_num}行数据格式错误: {e}")
                        error_count += 1
                        continue
        except FileNotFoundError:
            raise FileNotFoundError(f"数据文件未找到: {file_path}")
        except Exception as e:
            raise Exception(f"读取文件时发生错误: {e}")
            
        print(f"数据加载完成: 成功 {success_count} 条, 失败 {error_count} 条")
        
        if not self.records:
            raise ValueError("未找到有效数据")
    
    def _is_time_in_range(self, check_time: time) -> bool:
        """检查时间是否在分析范围内"""
        start_minutes = self.start_time.hour * 60 + self.start_time.minute
        end_minutes = self.end_time.hour * 60 + self.end_time.minute
        check_minutes = check_time.hour * 60 + check_time.minute
        
        # 处理跨天的情况
        if end_minutes < start_minutes:
            end_minutes += 24 * 60
            if check_minutes < start_minutes:
                check_minutes += 24 * 60
        
        return start_minutes <= check_minutes <= end_minutes
    
    def _create_time_segments(self) -> List[Tuple[time, time]]:
        """创建时间分段"""
        segments = []
        
        start_minutes = self.start_time.hour * 60 + self.start_time.minute
        end_minutes = self.end_time.hour * 60 + self.end_time.minute
        
        # 如果结束时间小于开始时间，表示跨天
        if end_minutes < start_minutes:
            end_minutes += 24 * 60
        
        current_minutes = start_minutes
        
        while current_minutes < end_minutes:
            # 计算当前段的开始时间
            start_hour = current_minutes // 60
            start_minute = current_minutes % 60
            if start_hour >= 24:
                start_hour -= 24
            start_segment = time(start_hour, start_minute)
            
            # 计算当前段的结束时间
            next_minutes = min(current_minutes + self.segment_minutes, end_minutes)
            end_hour = next_minutes // 60
            end_minute = next_minutes % 60
            if end_hour >= 24:
                end_hour -= 24
            end_segment = time(end_hour, end_minute)
            
            segments.append((start_segment, end_segment))
            current_minutes = next_minutes
        
        return segments
    
    def _time_to_minutes(self, t: time) -> int:
        """将时间转换为分钟数"""
        return t.hour * 60 + t.minute
    
    def _is_time_in_segment(self, check_time: time, segment: Tuple[time, time]) -> bool:
        """检查时间是否在分段内"""
        start_minutes = self._time_to_minutes(segment[0])
        end_minutes = self._time_to_minutes(segment[1])
        check_minutes = self._time_to_minutes(check_time)
        
        # 处理跨天的情况
        if end_minutes < start_minutes:
            end_minutes += 24 * 60
            if check_minutes < start_minutes:
                check_minutes += 24 * 60
        
        return start_minutes <= check_minutes < end_minutes
    
    def calculate_probabilities(self) -> pd.DataFrame:
        """计算各时间段的巡查概率"""
        if not self.records:
            raise ValueError("没有数据可供处理")
        
        segments = self._create_time_segments()
        total_records = len(self.records)
        
        print(f"分析 {total_records} 条记录，时间分段数: {len(segments)}")
        print(f"分析时间范围: {self.start_time.strftime('%H:%M')} - {self.end_time.strftime('%H:%M')}")
        
        results = []
        
        for start_seg, end_seg in segments:
            # 统计该时间段内的巡查次数
            count = sum(1 for record in self.records 
                       if self._is_time_in_segment(record.time, (start_seg, end_seg)))
            
            # 计算概率（使用拉普拉斯平滑避免零概率问题）
            probability = (count + 1) / (total_records + len(segments))
            
            # 计算原始频率
            frequency = count / total_records if total_records > 0 else 0
            
            results.append({
                'time_segment': f"{start_seg.strftime('%H:%M')}-{end_seg.strftime('%H:%M')}",
                'start_time': start_seg,
                'count': count,
                'frequency': frequency,
                'probability': probability,
                'segment_label': start_seg.strftime('%H:%M')
            })
        
        return pd.DataFrame(results)

class ProbabilityEstimator:
    """概率估计器类，使用核密度估计进行平滑"""
    
    @staticmethod
    def gaussian_kernel(x: float, bandwidth: float = 1.0) -> float:
        """高斯核函数"""
        return np.exp(-0.5 * (x / bandwidth) ** 2) / (bandwidth * np.sqrt(2 * np.pi))
    
    @staticmethod
    def estimate_smoothed_probabilities(df: pd.DataFrame, bandwidth: float = 0.1) -> pd.DataFrame:
        """使用核密度估计计算平滑概率"""
        if len(df) == 0:
            return df
            
        # 将时间转换为数值（0-1范围）
        time_values = np.array([row['start_time'].hour * 60 + row['start_time'].minute 
                              for _, row in df.iterrows()]) / (24 * 60)
        
        counts = df['count'].values
        total_count = counts.sum()
        
        # 如果总数为0，返回原始概率
        if total_count == 0:
            df['smoothed_probability'] = df['probability']
            return df
        
        # 计算核密度估计
        smoothed_probs = []
        for i, current_time in enumerate(time_values):
            weights = ProbabilityEstimator.gaussian_kernel(time_values - current_time, bandwidth)
            weighted_sum = np.sum(weights * counts)
            smoothed_prob = weighted_sum / (total_count * np.sum(weights))
            smoothed_probs.append(smoothed_prob)
        
        # 归一化
        smoothed_probs = np.array(smoothed_probs)
        if smoothed_probs.sum() > 0:
            smoothed_probs = smoothed_probs / smoothed_probs.sum()
        
        df['smoothed_probability'] = smoothed_probs
        return df

class VisualizationEngine:
    """可视化引擎类"""
    
    @staticmethod
    def calculate_label_interval(df_length: int, segment_minutes: int) -> int:
        """计算标签显示间隔，避免重叠"""
        # 根据时间段数量和分段时长动态计算间隔
        if df_length <= 12:
            return 1  # 少于12个时间段，全部显示
        elif df_length <= 24:
            return 2  # 12-24个时间段，每隔一个显示
        elif df_length <= 48:
            return 4  # 24-48个时间段，每隔三个显示
        elif df_length <= 72:
            return 6  # 48-72个时间段，每隔五个显示
        else:
            return max(8, df_length // 10)  # 更多时间段，按比例显示
    
    @staticmethod
    def calculate_font_size(df_length: int) -> int:
        """根据时间段数量计算字体大小"""
        if df_length <= 12:
            return 10
        elif df_length <= 24:
            return 9
        elif df_length <= 36:
            return 8
        elif df_length <= 48:
            return 7
        else:
            return 6
    
    @staticmethod
    def create_combined_plot(df: pd.DataFrame, segment_minutes: int, title: str = "老师巡查时间概率分布"):
        """创建条形图和曲线图的组合图表"""
        if len(df) == 0:
            print("没有数据可用于绘图")
            return None
            
        # 计算合适的图表大小
        fig_width = max(12, len(df) * 0.4)
        fig, ax1 = plt.subplots(figsize=(fig_width, 8))
        
        # 设置颜色
        bar_color = 'skyblue'
        line_color = 'red'
        
        # 条形图 - 实际频率
        x_positions = range(len(df))
        bars = ax1.bar(x_positions, df['frequency'], 
                      alpha=0.7, color=bar_color, label='实际频率', width=0.8)
        ax1.set_xlabel('时间段')
        ax1.set_ylabel('频率', color=bar_color)
        ax1.tick_params(axis='y', labelcolor=bar_color)
        
        # 第二个y轴 - 概率
        ax2 = ax1.twinx()
        line = ax2.plot(x_positions, df['smoothed_probability'], 
                       color=line_color, linewidth=2, marker='o', 
                       label='平滑概率')
        ax2.set_ylabel('概率', color=line_color)
        ax2.tick_params(axis='y', labelcolor=line_color)
        
        # 智能设置x轴标签
        label_interval = VisualizationEngine.calculate_label_interval(len(df), segment_minutes)
        font_size = VisualizationEngine.calculate_font_size(len(df))
        
        # 只显示部分标签，避免重叠
        x_ticks = []
        x_labels = []
        for i, label in enumerate(df['segment_label']):
            if i % label_interval == 0 or i == len(df) - 1:
                x_ticks.append(i)
                x_labels.append(label)
        
        plt.xticks(x_ticks, x_labels, rotation=45, ha='right', fontsize=font_size)
        
        # 添加图例
        lines1, labels1 = ax1.get_legend_handles_labels()
        lines2, labels2 = ax2.get_legend_handles_labels()
        ax1.legend(lines1 + lines2, labels1 + labels2, loc='upper left')
        
        plt.title(title)
        plt.tight_layout()
        
        return fig

def parse_arguments():
    """解析命令行参数"""
    parser = argparse.ArgumentParser(description='分析老师巡查时间概率分布')
    parser.add_argument('--segment', type=int, default=10, 
                       help='时间分段长度（分钟），默认10分钟')
    parser.add_argument('--start', type=str, default='18:00',
                       help='分析开始时间，格式 HH:MM，默认18:00')
    parser.add_argument('--end', type=str, default='21:00',
                       help='分析结束时间，格式 HH:MM，默认21:00')
    parser.add_argument('--bandwidth', type=float, default=0.05,
                       help='核密度估计的带宽参数，默认0.05')
    parser.add_argument('--data', type=str, default='data.txt',
                       help='数据文件路径，默认data.txt')
    parser.add_argument('--output', type=str, default='inspection_probability.png',
                       help='输出图片文件名，默认inspection_probability.png')
    parser.add_argument('--font', type=str, default='auto',
                       help='指定字体名称，默认auto自动检测')
    
    return parser.parse_args()

def debug_data_file(file_path: str):
    """调试数据文件格式"""
    print("调试数据文件格式:")
    print("=" * 50)
    try:
        with open(file_path, 'r', encoding='utf-8') as file:
            for i, line in enumerate(file, 1):
                print(f"行 {i}: '{line.strip()}'")
    except Exception as e:
        print(f"读取文件时出错: {e}")

def setup_chinese_font(font_name='auto'):
    """设置中文字体"""
    if font_name != 'auto':
        try:
            plt.rcParams['font.family'] = font_name
            print(f"使用指定字体: {font_name}")
            return True
        except:
            print(f"警告: 无法使用指定字体 '{font_name}'，尝试自动检测")
    
    return find_chinese_font()

def main():
    """主函数"""
    try:
        # 解析命令行参数
        args = parse_arguments()
        
        print(f"参数设置:")
        print(f"  时间分段: {args.segment}分钟")
        print(f"  分析时间范围: {args.start} - {args.end}")
        print(f"  带宽参数: {args.bandwidth}")
        print(f"  数据文件: {args.data}")
        print(f"  输出文件: {args.output}")
        print(f"  字体设置: {args.font}")
        print()
        
        # 设置中文字体
        setup_chinese_font(args.font)
        
        # 首先调试数据文件
        debug_data_file(args.data)
        print("\n" + "="*50 + "\n")
        
        # 初始化处理器
        processor = InspectionDataProcessor(
            segment_minutes=args.segment,
            start_time=args.start,
            end_time=args.end
        )
        
        # 加载数据
        processor.load_data(args.data)
        
        # 计算概率
        df = processor.calculate_probabilities()
        
        # 使用概率估计器进行平滑
        df = ProbabilityEstimator.estimate_smoothed_probabilities(df, bandwidth=args.bandwidth)
        
        # 创建可视化
        fig = VisualizationEngine.create_combined_plot(
            df, 
            segment_minutes=processor.segment_minutes,
            title=f"老师巡查时间概率分布 (分段: {processor.segment_minutes}分钟)"
        )
        
        # 显示结果表格
        print("\n巡查概率统计表:")
        print("=" * 80)
        result_df = df[['time_segment', 'count', 'frequency', 'smoothed_probability']].copy()
        result_df['frequency'] = result_df['frequency'].apply(lambda x: f"{x:.4f}")
        result_df['smoothed_probability'] = result_df['smoothed_probability'].apply(lambda x: f"{x:.4f}")
        print(result_df.to_string(index=False))
        
        # 显示统计信息
        print(f"\n统计信息:")
        print(f"总巡查次数: {len(processor.records)}")
        print(f"时间段数量: {len(df)}")
        print(f"最频繁巡查时间段: {df.loc[df['count'].idxmax(), 'time_segment']}")
        print(f"最高概率时间段: {df.loc[df['smoothed_probability'].idxmax(), 'time_segment']}")
        
        # 保存图表
        if fig is not None:
            plt.savefig(args.output, dpi=300, bbox_inches='tight')
            print(f"图表已保存为: {args.output}")
            plt.show()
        else:
            print("无法生成图表，没有有效数据")
        
    except Exception as e:
        print(f"错误: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()