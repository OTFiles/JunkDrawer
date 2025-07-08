#JunkDrawer
放一些有趣的玩意

##bdfy.py
基于`baidu_translate_api`项目实现的较为完整功能的小程序，你应该先安装`baidu_translate_api`:
```shell
pip install baidu_translate_api
```

##CubeGL.cpp
渲染一个正方体(在终端)，详细的编译方法见文件注释

##Hypercube.html
在网页渲染一个超立方体，目前支持4d和5d，使用浏览器打开这个文件即可

##LifeGame.cpp
生命游戏(在终端)，详细的编译方法见文件注释。
当添加`-z <数字>`参数时，在演算时会提前演算
默认模式:设计模式，按回车或者空格键切换细胞状态，按`Q`退出
命令模式:按`C`进入，按`ESC`退出
- `save [文件名]` - 保存当前模式到文件（默认: pattern.lif）
- `load [文件名]` - 从文件加载模式（默认: pattern.lif）
- `clear` - 清空所有细胞
- `rand x y w h` - 在指定区域随机生成细胞
演算模式:按`Y`进入，按`Q`退出
移动:上下左右键移动光标，wasd移动地图

##mergeText.py
将`in`目录下的所有文件合并

##MiGong.cpp
一个移动就会刷新的迷宫，并会显示到终点的路径

##NotifyRPG.sh
仅支持在termux上运行，调用`termux-notification`和`termux-notification-remove`实现的一个简单文字冒险游戏