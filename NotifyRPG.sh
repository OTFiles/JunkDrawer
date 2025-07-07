#!/bin/bash

# 黑暗主题文字冒险游戏（修复探索洞穴问题）
# 使用Termux通知系统进行交互
# 必须使用安卓端并且使用termux进行游玩

# 清理旧通知
termux-notification-remove "dark_game" &> /dev/null

# 获取脚本绝对路径（核心修复）
SCRIPT_PATH=$(realpath "$0")

## 场景函数定义

# 游戏开始场景
scene_start() {
    termux-notification --id "dark_game" -t "黑暗深处" -c "你醒来发现自己身处一片漆黑的地牢中，四周弥漫着腐臭味。耳边传来滴水声和远处模糊的呻吟..." \
        --button1 "环顾四周" \
        --button1-action "bash '$SCRIPT_PATH' scene_intro"
}

# 初始场景
scene_intro() {
    termux-notification --id "dark_game" -t "黑暗深处" -c "借着石缝透进的微光，你发现：\n左边是生锈的铁门\n右边有潮湿的通道\n地上有把断剑" \
        --button1 "检查铁门" \
        --button1-action "bash '$SCRIPT_PATH' scene_iron_door" \
        --button2 "检查通道" \
        --button2-action "bash '$SCRIPT_PATH' scene_wet_passage" \
        --button3 "拿起断剑" \
        --button3-action "bash '$SCRIPT_PATH' scene_broken_sword"
}

# 铁门场景
scene_iron_door() {
    termux-notification --id "dark_game" -t "铁门" -c "铁门被厚重的锁链缠绕，但锁已锈蚀。你用力一推，门吱呀一声开了..." \
        --button1 "进入" \
        --button1-action "bash '$SCRIPT_PATH' scene_mysterious_room" \
        --button2 "原路返回" \
        --button2-action "bash '$SCRIPT_PATH' scene_footsteps"
}

# 神秘房间场景
scene_mysterious_room() {
    termux-notification --id "dark_game" -t "未知房间" -c "房间中央有口古井，井边刻着神秘符文。井底传来微弱绿光..." \
        --button1 "查看符文" \
        --button1-action "bash '$SCRIPT_PATH' scene_ancient_runes" \
        --button2 "跳入井中" \
        --button2-action "bash '$SCRIPT_PATH' scene_well_bottom"
}

# 符文场景（坏结局）
scene_ancient_runes() {
    termux-vibrate -d 1000
    termux-notification --id "dark_game" -t "古老符文" -c "符文突然发光！井中伸出枯骨之手将你拖入深渊..." \
        --button1 "重新开始" \
        --button1-action "bash '$SCRIPT_PATH' scene_start"
}

# 井底场景（好结局）
scene_well_bottom() {
    termux-notification --id "dark_game" -t "井底世界" -c "你坠入冰冷的水中，发现水下有条通道。游过通道后，来到一个满是发光蘑菇的洞穴..." \
        --button1 "探索洞穴" \
        --button1-action "bash '$SCRIPT_PATH' scene_freedom"
}

# 自由场景（结局）- 修复点
scene_freedom() {
    # 关键修复：将语音命令放入后台执行，避免阻塞
    ( termux-tts-speak -l zh-CN "恭喜你逃出地牢！但更大的黑暗在等着你..." & )
    
    termux-notification --id "dark_game" -t "自由" -c "你逃出了地牢，但眼前的景象让你绝望：整个世界已被黑暗吞噬..." \
        --button1 "重新开始" \
        --button1-action "bash '$SCRIPT_PATH' scene_start"
}

# 脚步声场景
scene_footsteps() {
    termux-notification --id "dark_game" -t "脚步声" -c "返回时发现守卫正在巡逻！你急忙躲进阴影中..." \
        --button1 "伏击守卫" \
        --button1-action "bash '$SCRIPT_PATH' scene_combat" \
        --button2 "等待" \
        --button2-action "bash '$SCRIPT_PATH' scene_discovered"
}

# 战斗场景
scene_combat() {
    termux-notification --id "dark_game" -t "战斗" -c "你用断剑刺中守卫！获得钥匙和火把。火把照亮了前方的三条岔路..." \
        --button1 "左路" \
        --button1-action "bash '$SCRIPT_PATH' scene_dead_end" \
        --button2 "中路" \
        --button2-action "bash '$SCRIPT_PATH' scene_middle_path" \
        --button3 "右路" \
        --button3-action "bash '$SCRIPT_PATH' scene_right_path"
}

# 死路场景
scene_dead_end() {
    termux-notification --id "dark_game" -t "死路" -c "尽头是塌方的石堆，但你在碎石中发现一袋金币" \
        --button1 "返回" \
        --button1-action "bash '$SCRIPT_PATH' scene_combat"
}

# 被发现场景（坏结局）
scene_discovered() {
    termux-notification --id "dark_game" -t "被发现" -c "守卫的同伴赶来！你被包围了..." \
        --button1 "重新开始" \
        --button1-action "bash '$SCRIPT_PATH' scene_start"
}

# 潮湿通道场景
scene_wet_passage() {
    termux-notification --id "dark_game" -t "潮湿通道" -c "通道尽头是向下的石阶，传来阵阵冷风..." \
        --button1 "向下走" \
        --button1-action "bash '$SCRIPT_PATH' scene_underground_tomb" \
        --button2 "原路返回" \
        --button2-action "bash '$SCRIPT_PATH' scene_intro"
}

# 地下墓穴场景
scene_underground_tomb() {
    termux-notification --id "dark_game" -t "地下墓穴" -c "你踏入满是棺椁的墓室。突然所有棺盖同时震动！" \
        --button1 "逃跑" \
        --button1-action "bash '$SCRIPT_PATH' scene_maze" \
        --button2 "战斗" \
        --button2-action "bash '$SCRIPT_PATH' scene_undead_awakening"
}

# 迷宫场景
scene_maze() {
    termux-notification --id "dark_game" -t "迷宫" -c "你在黑暗的迷宫中迷失方向，听到四面八方传来低语..." \
        --button1 "左转" \
        --button1-action "bash '$SCRIPT_PATH' scene_altar" \
        --button2 "右转" \
        --button2-action "bash '$SCRIPT_PATH' scene_dead_end"
}

# 祭坛场景（特殊结局）
scene_altar() {
    termux-notification --id "dark_game" -t "祭坛" -c "发现黑色祭坛，中央悬浮着一颗跳动的心脏" \
        --button1 "触碰心脏" \
        --button1-action "bash '$SCRIPT_PATH' scene_dark_lord"
}

# 黑暗领主结局
scene_dark_lord() {
    # 关键修复：将语音命令放入后台执行
    ( termux-tts-speak -l zh-CN "你吸收了黑暗力量，成为新的墓穴主人！" & )
    
    termux-notification --id "dark_game" -t "黑暗领主" -c "你获得了永生，但代价是永远被困在黑暗中..." \
        --button1 "重新开始" \
        --button1-action "bash '$SCRIPT_PATH' scene_start"
}

# 亡灵苏醒场景（坏结局）
scene_undead_awakening() {
    termux-notification --id "dark_game" -t "亡灵苏醒" -c "无数枯骨从棺中爬出！你被亡灵淹没了..." \
        --button1 "重新开始" \
        --button1-action "bash '$SCRIPT_PATH' scene_start"
}

# 断剑场景
scene_broken_sword() {
    termux-vibrate -d 500
    termux-notification --id "dark_game" -t "断剑" -c "剑柄刻着：\"唯有黑暗吞噬光明\"..." \
        --button1 "继续探索" \
        --button1-action "bash '$SCRIPT_PATH' scene_intro"
}

# 中道路径场景
scene_middle_path() {
    termux-notification --id "dark_game" -t "幽暗长廊" -c "长廊两侧有无数雕像，它们的眼睛似乎在跟着你移动..." \
        --button1 "快速通过" \
        --button1-action "bash '$SCRIPT_PATH' scene_statue_room" \
        --button2 "检查雕像" \
        --button2-action "bash '$SCRIPT_PATH' scene_statue_curse"
}

# 雕像房间场景
scene_statue_room() {
    termux-notification --id "dark_game" -t "雕像大厅" -c "你安全通过长廊，来到一个布满蜘蛛网的大厅，中央有一尊巨大的恶魔雕像" \
        --button1 "检查雕像" \
        --button1-action "bash '$SCRIPT_PATH' scene_demon_statue" \
        --button2 "寻找出口" \
        --button2-action "bash '$SCRIPT_PATH' scene_secret_exit"
}

# 右路场景
scene_right_path() {
    termux-notification --id "dark_game" -t "血腥通道" -c "墙壁上布满干涸的血迹，空气中弥漫着铁锈味..." \
        --button1 "继续前进" \
        --button1-action "bash '$SCRIPT_PATH' scene_torture_chamber" \
        --button2 "返回" \
        --button2-action "bash '$SCRIPT_PATH' scene_combat"
}

# 刑讯室场景
scene_torture_chamber() {
    termux-vibrate -d 1500
    termux-notification --id "dark_game" -t "刑讯室" -c "房间中摆放着各种恐怖的刑具，墙上挂着一具还在滴血的尸体..." \
        --button1 "检查尸体" \
        --button1-action "bash '$SCRIPT_PATH' scene_corpse" \
        --button2 "快速离开" \
        --button2-action "bash '$SCRIPT_PATH' scene_escape_torture"
}

# 主函数
main() {
    # 根据参数调用对应场景
    case "$1" in
        scene_start) scene_start ;;
        scene_intro) scene_intro ;;
        scene_iron_door) scene_iron_door ;;
        scene_mysterious_room) scene_mysterious_room ;;
        scene_ancient_runes) scene_ancient_runes ;;
        scene_well_bottom) scene_well_bottom ;;
        scene_freedom) scene_freedom ;;
        scene_footsteps) scene_footsteps ;;
        scene_combat) scene_combat ;;
        scene_dead_end) scene_dead_end ;;
        scene_discovered) scene_discovered ;;
        scene_wet_passage) scene_wet_passage ;;
        scene_underground_tomb) scene_underground_tomb ;;
        scene_maze) scene_maze ;;
        scene_altar) scene_altar ;;
        scene_dark_lord) scene_dark_lord ;;
        scene_undead_awakening) scene_undead_awakening ;;
        scene_broken_sword) scene_broken_sword ;;
        scene_middle_path) scene_middle_path ;;
        scene_statue_room) scene_statue_room ;;
        scene_right_path) scene_right_path ;;
        scene_torture_chamber) scene_torture_chamber ;;
        *) scene_start ;; # 默认从开始场景启动
    esac
}

# 启动游戏
main "$@"