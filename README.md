# Data Replay

基于 Qt 的桌面数据回放工具，支持多路视频、CAN 日志和音频回放。

## 构建

已验证工具链：Qt 6.7.2（MinGW 64 位）、MinGW 11.2、CMake、Ninja。
Qt 需要 Widgets、Multimedia、MultimediaWidgets、Concurrent 模块。

将 CMake、Ninja 和 MinGW 的 bin 目录加入 PATH，在项目根目录执行以下命令。
请将 Qt SDK 路径替换为本机实际路径：

```powershell
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="D:/Qt/6.7.2/mingw_64"
cmake --build build/release --parallel 4
```

输出程序为 `build/release/Data_replay.exe`。分发前需使用对应 Qt SDK 的
`windeployqt --release --compiler-runtime` 部署 Qt 插件、音视频依赖及编译器运行库。

Linux 版尚未构建或验证。

## 数据目录

通过窗口选择视频、CAN 日志和音频文件夹，也可放到程序同级的
`视频文件/`、`log文件/`、`音频文件/` 目录中。

## 仓库范围

仓库保留程序源码、界面资源、CMake 配置和本说明。
`docs/`、`packaging/`、`build/`、`release/`、`local-archive/`、
本地参考资料、录制数据、日志和 IDE 配置均不提交 Git。
