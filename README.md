# XiwuCLAW

科技感赛博朋克风格的 JUCE VST3 音频插件

## 功能特性

- 舞台网格绘图 - 在 120x80 网格上绘制图案并生成 MIDI
- MIDI 文件生成 - 将网格图案转换为 MIDI 音符
- 音色库管理 - 内置音色预设系统
- EQ 四段均衡 - 低/低中/高中/高四段频率调节
- 科技感 UI - 赛博朋克风格界面，霓虹发光效果

## 安装

从 [Releases](https://github.com/xchord5/XiwuCLAW/releases) 下载最新版本：

- `XiwuCLAW_Windows.zip` - Windows 版本（包含 standalone 和 VST3 插件）

解压后将文件复制到你的 DAW 插件目录即可使用。

## 系统要求

- Windows 10/11
- 支持 VST3 的 DAW（如 Cubase, Logic Pro, Ableton Live 等）

## 构建

```bash
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j4
```

## License

MIT
