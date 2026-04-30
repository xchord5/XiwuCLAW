# XiwuCLAW

基于 Rust + LLM 的 AI 音乐制作助手，通过 QQ 语音交互与 Ableton Live 协同创作。

## 核心功能

### 音乐生成
- **旋律生成 (melody_generate)** — 用户指定调式和提示词，生成旋律 MIDI
- **和声生成 (harmony_generate)** — 生成和声 MIDI
- **鼓组生成 (drum_generate)** — 生成鼓点 MIDI
- **音色生成 (tone_generate)** — 生成音色 MIDI
- **多轨编排 (breakdown_generate)** — 将鼓组/节奏/旋律轨道编排为完整多轨 MIDI

### 和声智能路由
每次旋律生成前自动检测和声数据，按优先级降级：
1. **用户指定** — 对话中提供和声序列（如 `Am-F-C-G`），直接使用
2. **AbletonOSC 路线** — 从 Ableton loop 区域导出 MIDI → harmony_detect 分析和声 → 自动创建 `harmonyMARK` 可视化轨道
3. **Probe 路线** — 从 VST3 Probe 插件获取 MIDI → 分析和声
4. **默认兜底** — 无可用时使用默认和声进行

### harmonyMARK 可视化
和声检测完成后自动在 Ableton 中创建：
- 新建名为 `harmonyMARK` 的 MIDI 轨道
- 为每个和声在对应时间点创建 session clip，clip 名显示和声名
- clip 长度自动计算，确保相互不覆盖
- 重检测时清空旧 clip 再创建新的
- 轨道锁定不可编辑

### 调式强制要求
用户必须指定调式（如 `C大调`、`A小调`、`Cmaj`、`Am`），否则拒绝生成并反馈提示。

### 语音转 MIDI
- **口嗨一段** — 接收用户语音，转为旋律 MIDI
- **动次打次** — 接收 beatbox 音频，转为鼓组 MIDI

### VST3 状态显示
通过 OSC 实时显示工具执行状态到 VST3 插件面板：
`WaitingForInput → GettingInfo → Generating(0-100%) → Placing → Completed/Failed`

### 单轨道后处理
MIDI 生成后自动执行：
1. 剪贴板写入 MIDI 路径
2. VST 蓝块通知
3. AbletonOSC 自动放置到选择轨道

### LLM 集成
- **Ollama 专用模式** — 本地模型，定制化系统提示词，仅处理音乐问题
- **OpenAI 兼容接口** — 支持各种 OpenAI API 格式的服务端
- **多模型支持** — 可配置不同模型用于不同任务

### QQ 渠道
通过 NapCat/OneBot 协议与 QQ 交互，支持文本和语音消息。

## 项目结构

```
xiwuclaw-rs/
├── Cargo.toml                  # Workspace 配置
├── crates/
│   ├── xiwuclaw-core/          # 核心类型、消息结构、错误定义
│   ├── xiwuclaw-config/        # 配置系统
│   ├── xiwuclaw-agent/         # Agent 核心循环、工具调度
│   ├── xiwuclaw-tools/         # 工具系统（音乐生成、路由、后处理）
│   │   ├── music_tool_executor.rs  # 通用生成管道
│   │   ├── music_routing.rs        # 数据源路由器
│   │   ├── abletonosc.rs           # AbletonOSC 客户端
│   │   ├── probe.rs                # VST3 Probe 通信
│   │   ├── vst_display.rs          # VST 状态显示
│   │   ├── single_track_post.rs    # 单轨道后处理
│   │   ├── qq_interaction.rs       # QQ 交互
│   │   └── music_tools.rs          # 音乐工具定义
│   ├── xiwuclaw-providers/     # LLM 提供者（Ollama/OpenAI 兼容）
│   ├── xiwuclaw-channels/      # 消息渠道抽象
│   ├── channel-qq/             # QQ/NapCat 渠道
│   ├── xiwuclaw-web/           # Web API 服务
│   └── xiwuclaw-memory/        # 持久化记忆系统
├── bins/xiwuclaw/              # 主程序入口
└── juce-vst3-gain/             # VST3 插件（含 Probe 版本）
```

## 快速开始

```bash
# 开发构建
cargo build

# 发布构建
cargo build --release

# 运行
cargo run --bin xiwuclaw

# 测试
cargo test --workspace
```

## 部署结构

```
H:\xiwuclawALL\
├── gateway\                  # 主程序运行目录
│   └── xiwuclaw.exe          # 主程序
├── music_tools\              # Python 音乐工具
│   └── music_tools_manager.py
├── _python_runtime\          # 独立 Python 运行环境
│   └── python.exe
└── AbletonOSC\               # Ableton Live OSC 控制
```

## 配置文件

创建 `config.yaml` 或设置环境变量配置音乐服务、QQ 渠道等参数。

## 许可证

MIT License
