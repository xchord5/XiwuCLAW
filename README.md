# XiwuCLAW

AI 音乐制作助手 —— 基于 Rust + LLM + VST3 插件的语音交互音乐编曲系统。

---

## 项目简介

XiwuCLAW 是一套完整的 AI 音乐编曲工具链，通过 QQ 语音消息与用户交互，将 AI 生成的旋律/和声/鼓点 MIDI 自动放置到 DAW（如 Ableton Live）中。核心组件包括：

- **Rust Agent 主程序** — LLM 驱动的智能体，负责意图理解、工具调度、服务编排
- **VST3 插件** — 嵌入 DAW 的合成器 + MIDI Probe，支持 OSC 双向通信
- **Python 音乐工具** — 和声检测、节奏模式、语音转 MIDI 等专业分析

---

## VST3 插件架构（`src/` + `src-probe/`）

本项目包含**两个独立的 VST3 插件**，基于 JUCE 框架构建，CMake 编译为 `.vst3` 文件。

### 主插件 `XiwuCLAW`（`src/`） — Instrument / Synth

**定位**：AI 合成引擎 + DAW 桥接 + 状态显示

#### 合成引擎

- **14 声部复音合成器**，继承自 `juce::Synthesiser`
- 每个声部 `XiwuVoice` 使用 **6 种振荡器混合**：正弦(sine)、锯齿(saw)、方波(square)、三角波(triangle)、噪声(noise)、次低八度(sub)
- 振荡器混合比例由 `ToneSpec` 音色规格定义
- **tanh 软削波失真**：`std::tanh(sample * (1.0f + tone.drive * 3.8f))`
- **立体声宽度控制**（leftFactor/rightFactor 微调）
- **ADSR 包络**（attack/decay/sustain/release）
- **4 段 Master Output EQ**：Low(180Hz) / LowMid(1200Hz) / HighMid(5200Hz) / High，±18dB 增益，tanh 软限幅输出

#### 音色系统

- 5 种类别：Bass、Lead、Pad、Pluck、Fx
- 音色来源：
  - 编译时嵌入（`XiwuToneData` 二进制数据）
  - 运行时从 `XIWU_TONE_USERLIB_DIR`、`~/.xiwuclaw/workspace/tone_userlib/` 动态加载
- 支持两种 JSON 格式：完整 `tone_spec_v1` 和降级 `tone_type`
- 支持通过 `/xiwu/vst/tone/load_json` OSC 端点**热加载** LLM 生成的音色

#### OSC 通信网关（`OscGatewayBridge`）

- UDP 端口 **3921**，监听来自 Rust Agent 的 OSC 消息
- 地址前缀：`/xiwu/vst`
- 解析 OSC 格式为 "address + JSON payload string"，路由到对应处理函数

**OSC 端点列表**：

| 端点 | 功能 |
|------|------|
| `/xiwu/vst/ping` | 心跳检测，确认插件在线 |
| `/xiwu/vst/transport/get` | 获取宿主传输状态（BPM、拍号、播放头位置） |
| `/xiwu/vst/midi/place` | 接收 MIDI 放置请求，将生成的 MIDI 放入 DAW |
| `/xiwu/vst/tone/load_json` | 加载 LLM 生成的音色 JSON 配置 |
| `/xiwu/vst/task/busy` | 设置/清除外部任务忙状态 |
| `/xiwu/vst/status` | 接收工具运行状态（WaitingForInput → Generating → Completed/Failed） |
| `/xiwu/vst/midi/collect/full` | 全量 MIDI 收集 |
| `/xiwu/vst/midi/collect/selection` | 选区 MIDI 收集 |
| `/xiwu/vst/track/active/snapshot/get` | 获取在线 Probe 快照列表 |

#### Probe 状态接收（`ProbeUdpBridge`）

- UDP 端口 **3922**，接收来自 Probe 插件的状态消息
- Probe 状态结构：midiActivity、noteDensity、audioPeak、transportHint、midiDetails
- **TTL 机制**：3 秒无心跳视为离线（`kProbeTtlMs = 3000`）
- 自动清理过期条目（`pruneExpiredProbes()`）

#### 参数系统

6 个参数通过 `AudioProcessorValueTreeState` 管理：

| 参数 | 范围 | 默认值 |
|------|------|--------|
| `tone_mode` | 动态（音色数量） | 0 |
| `master_gain` | 0.0 – 1.2 | 0.82 |
| `eq_low` | 0.0 – 1.0 | 0.5 |
| `eq_low_mid` | 0.0 – 1.0 | 0.5 |
| `eq_high_mid` | 0.0 – 1.0 | 0.5 |
| `eq_high` | 0.0 – 1.0 | 0.5 |

### 主插件编辑器 — 赛博朋克 UI

- 窗口尺寸：**1180 x 760** 像素
- 双页面：**Stage**（舞台）和 **Eq**（均衡器）

**Stage 页面**：
- 音色选择 ComboBox + 主音量旋钮
- **"GENERATE MIDI"** 按钮 — 将网格发送至 Gateway
- 中央 **120x80 MIDI 矩阵画布**（9600 单元格）
  - 鼠标绘制 / Alt 擦除，3 级强度
  - Level 0 = 赛博青，Level 1 = 电紫，Level 2 = 霓虹粉
- MIDI Tile 拖拽区域
- 底部状态面板：工具运行状态 + 进度条 + Probe 信息 + 选区 MIDI 列表

**Eq 页面**：
- 四个旋钮均匀分布：LOW（青）/ LOW MID（绿）/ HIGH MID（粉）/ HIGH（紫）

**视觉特效**：
- 深空黑蓝 → 午夜蓝 → 深渊黑 渐变背景
- `drawNeonGlow()`：8 层辉光圆角矩形
- `paintTechKnob()`：LED 光环旋钮，渐变色弧
- **30 粒子系统**：随机颜色和速度，边界反弹，透明度脉冲
- 正弦扫描线网格动画
- 剪贴板 Toast 通知（4 秒自动消失，8 个闪烁星点）
- Busy 遮罩：外部任务运行时缓存网格，显示忙碌标记

### Probe 插件 `XiwuCLAW Probe`（`src-probe/`） — MIDI Effect

**定位**：MIDI 数据采集与分析，可多实例部署到不同轨道

#### MIDI 活动跟踪器（`MidiActivityTracker`）

- 记录所有 MIDI 音符事件（timeMs、noteNumber、velocity、isOn）
- `events`：最近 **500ms 窗口**内的活动事件（密度计算）
- `allNotes`：最多 **60 秒历史**的完整音符记录
- `getMidiDetails(windowStartMs, windowEndMs)`：返回指定时间窗口的 MIDI 详情 JSON
- `exportMidiFile()`：导出标准 MIDI 文件（480 ticks/quarter，120 BPM，4/4）

#### UDP 通信（`UdpThread`）

- 端口 **3922**（从 `%APPDATA%\XiwuCLAW\probe_session.json` 读取）
- 支持命令：
  - `"get_midi_details"` — 返回时间窗口内的 MIDI 详情
  - `"ping"` — 返回 pong 响应
- 启动时向主插件发送 `"announce"` 注册消息（含 category、track_number、probeId）

#### 参数系统

| 参数 | 值 | 默认值 |
|------|-----|--------|
| `category` | Drums / Rhythm / Melody | Drums |
| `track_number` | 1 – 64 | 1 |

#### Probe 编辑器

- 窗口：**380 x 200**，深紫背景
- Category 下拉框 + Track Number 滑块
- UDP 监听状态实时显示（2Hz 刷新）
- 状态颜色：已连接=浅绿，初始化=橙色

### 两插件数据流

```
[DAW MIDI Track]
      | MIDI In
      v
+--------------------------+
| XiwuCLAW Probe           |  MIDI Effect
| - MidiActivityTracker    |  跟踪音符
| - UdpThread (:3922)      |  状态上报
+--------------------------+
      | UDP announce / midi_details
      v
+--------------------------+
| XiwuCLAW (主插件)         |  Instrument
| - ProbeUdpBridge (:3922) |  接收 Probe 状态
| - OscGatewayBridge (:3921)|  OSC 网关
| - SynthEngine (14 voices)|  合成输出
| - 4-band Master EQ       |  输出均衡
+--------------------------+
      | OSC (/xiwu/vst/*)
      v
[Rust Agent 主程序]
```

---

## Rust 主程序功能概览

### Agent 核心

- **消息处理优先级链**：VoiceRhythm → TTS → Music → Mountain(HTTP DAW控制) → HTTP Service → WASM Plugin → LLM 对话
- **Ollama 外部模式**：独立系统提示词，本地模型，多模型自动回退链
- **工具迭代循环**：LLM 返回 tool_calls → 执行 → 返回结果 → 继续（最大迭代可配置）
- **Session 历史**：最近 40 条消息上下文
- **宫殿命令**：`宫中奏乐` / `宫中天音` / `宫中奇闻` / `宫中工坊`

### 音乐工具（11 个）

| 工具 | 功能 | 执行方式 |
|------|------|----------|
| `melody_generate` | 旋律 MIDI 生成 | HTTP 音乐服务 |
| `harmony_generate` | 和声 MIDI 生成 | HTTP 音乐服务 |
| `drum_generate` | 鼓点 MIDI 生成 | HTTP 音乐服务 |
| `tone_generate` | 音色/合成器 Patch 生成 | Tone Chain |
| `harmony_detect` | MIDI 和弦检测 | Python (music21) |
| `pattern_generate` | 节奏模式自然语言搜索 | Python |
| `rhythm_generate` | 基于命名模式的和弦节奏 MIDI | Python (mido) |
| `voice_to_midi` | 语音/Beatbox 转节奏 MIDI | Python |
| `voice_to_drum` | Beatbox 转鼓 MIDI | Python |
| `breakdown_generate` | 多轨编排 MIDI 生成 | Python |
| `dense_midi_generate` | 密集 32 分音符 MIDI 生成 | Python |

### 其他服务

- **TTS 语音合成** — 聊天回复转语音，4 次重试
- **语音传音** — 两步交互：激活 → 接收语音 → YIN 基频检测 → MIDI
- **DAW 远程控制（Mountain）** — 自然语言意图推断（播放/停止/跳小节/静音/音量/速度/录音）
- **WASM 沙箱插件** — `.xiwuplugin` 文件加载，沙箱能力控制
- **通用 HTTP 触发器** — 关键词/正则/command 匹配，GET/POST 请求

### 通用工具（14 个）

`read_file` / `write_file` / `edit_file` / `append_file` / `list_dir` / `exec` / `web_search` / `web_fetch` / `send_file` / `send_image_base64` / `cron` / `find_skills` / `install_skill` / `write_memory`

---

## 下一步：基于 Hermes 的自主学习音乐编曲智能体

当前工具链是**命令驱动**的 —— 用户发指令，Agent 执行。下一步将升级为 **Hermes 协议驱动的自主编曲智能体**，实现以下目标：

### Hermes 协议

Hermes 是一个异步事件驱动的通信协议，原本用于智能家居/语音助手。我们将它改造为**音乐编曲领域的事件总线**：

```
[用户 QQ] ←→ [NapCat/OneBot] ←→ [Agent 消息总线]
                                      |
                    +-----------------+-----------------+
                    |                 |                 |
              [Music Event]    [DAW Event]       [Memory Event]
                    |                 |                 |
              [生成节点]         [状态节点]         [学习节点]
```

### 自主学习能力

1. **和声记忆库（Harmony Memory）**
   - 每次和声检测的结果自动存入持久化记忆
   - 记录：调式 → 和声序列 → 用户反馈 → 风格标签
   - 下次生成时从记忆中检索相似和声进行，而非每次从零分析
   - 支持"上次那种感觉再来一遍"的自然语言指令

2. **节奏模式学习**
   - 用户接受/拒绝的节奏模式自动标记
   - 基于用户偏好自动调整 pattern_generate 的权重
   - 新发现的节奏变体自动加入模式库

3. **音色偏好学习**
   - 用户选择的音色类型、EQ 参数、风格特征自动记录
   - ToneService 的 LLM prompt 根据历史偏好动态调整
   - 支持"换个暗一点的 Pad"这类模糊指令

4. **曲目结构理解**
   - 通过 Probe 持续采集 MIDI 数据，构建曲目时间线
   - 自动识别：Intro → Verse → Chorus → Bridge → Outro
   - 支持"在副歌前面加个过渡"等结构级指令

### 自主编曲流程

```
用户："帮我做一段 C 大调的主歌"
  ↓
[意图解析] → 调式: C大调, 结构: Verse, 时长: ~8小节
  ↓
[记忆检索] → 查找历史 C 大调作品的和声模式
  ├── 找到 → 推荐："上次 C 大调用的 Am-F-C-G，这次也这样？"
  └── 未找到 → 进入和声路由检测（AbletonOSC → Probe → Default）
  ↓
[和声确认] → 用户确认或修改
  ↓
[旋律生成] → 基于和声 + BPM + 拍号 + 风格偏好
  ↓
[节奏生成] → 根据历史偏好选择节奏模式
  ↓
[鼓点添加] → 从 drum style bank 中语义匹配
  ↓
[自动编排] → breakdown_generate 将多轨组合
  ↓
[DAW 放置] → VST OSC 自动放置到对应轨道
  ↓
[回放确认] → Mountain Service 自动播放片段
  ↓
[反馈学习] → 用户"不错"/"换一下" → 更新偏好记忆
```

### 事件驱动节点

每个节点是独立的 Hermes 处理器，通过事件总线通信：

| 节点 | 订阅事件 | 发布事件 |
|------|----------|----------|
| `harmony_detector` | `new_midi_received` | `harmony_analyzed`, `harmony_memory_updated` |
| `melody_generator` | `harmony_ready` | `melody_generated`, `midi_placed` |
| `rhythm_picker` | `harmony_ready`, `user_feedback` | `rhythm_selected` |
| `arranger` | `all_tracks_ready` | `breakdown_placed` |
| `memory_learner` | `user_feedback`, `tool_completed` | `preference_updated` |
| `style_classifier` | `session_started` | `style_profile_ready` |

### 技术路线

1. **第一阶段**：实现 Harmony Memory 持久化 + 检索
   - 将 harmony_detect 结果存入 JSON 记忆库
   - melody_generate 前自动检索相似和声
   - QQ 交互确认机制

2. **第二阶段**：实现事件驱动工具链
   - 将当前的线性执行改为 Hermes 事件流
   - 每个工具独立为节点，通过事件触发
   - 支持并行节点（旋律+鼓点同时生成）

3. **第三阶段**：自主编曲决策
   - Agent 根据曲目结构自动决定需要哪些轨道
   - 根据风格自动选择和声/节奏/音色
   - 用户只需说"做首流行歌"，系统自主完成全流程

---

## XiwuModel 音乐工具链模型

XiwuModel 是**独立自研的音乐工具链模型**，以 4.5 亿参数规模，围绕音乐编曲、混音、母带的专业流程完整打造。模型内嵌丰富的 MIDI 编曲常规样式知识库，是**业界第一个专注于编曲音乐工具链的模型**。

### 核心优势

XiwuModel 的真正竞争力不在于单点生成能力，而在于**与 XiwuCLAW 工具链的深度结合**：

1. **模型与工具链的双向驱动** — XiwuModel 不是孤立运行的推理引擎，而是被 XiwuCLAW 的 Agent 系统调度、后处理管道修正、Probe 反馈循环验证的**系统级组件**。模型输出可被 Harmony Detect 验证、被 Single Track Post 量化修整、被 Breakdown Generate 多轨拆分重组，形成闭环。

2. **系统调度深度整合** — 模型推理与数据源路由（AbletonOSC → Probe → Default）、和声记忆库检索、风格偏好学习、VST3 状态显示无缝联动。用户一句"做个 C 大调的流行"，系统自动完成：记忆检索 → 条件提取 → 模型推理 → 后处理 → DAW 落轨 → 回放确认 → 反馈学习。

3. **MIDI 编曲常规样式内嵌** — 模型训练数据不仅包含旋律/和声/鼓点的音符序列，更内嵌了大量 MIDI 编曲中的经典手法：填充（fill）、过门（transition）、力度动态（dynamics）、声部对位（counterpoint）等专业编曲技巧。

4. **工具链模型 vs 纯生成模型** — 传统音乐 AI 模型是"输入提示 → 输出 MIDI/音频"的单向生成。XiwuModel 是**工具链模型**：它的输出可以被后续工具修正、验证、重组；它的输入可以被前置工具增强（BPM、拍号、和声记忆、风格偏好）；它的生成过程可以被 VST3 面板实时显示。这种深度整合是其他模型无法做到的。

### 模型架构

| 参数 | 值 |
|------|-----|
| 参数量 | **4.5 亿**（450M） |
| 架构 | Transformer Decoder-only |
| 训练目标 | 自回归 MIDI 序列建模（音符、和弦、节拍、力度） |
| 输出 | 结构化 MIDI 数据（非音频波形） |
| 定位 | 轻量化、本地可部署、低延迟生成 |

### 设计动机

现有音乐生成模型（如 MusicGen、Suno、Udio）存在以下局限：

1. **闭源** — 依赖云端 API，无法离线使用
2. **体积庞大** — 数十亿参数，消费级硬件无法运行
3. **输出为音频** — 无法直接在 DAW 中编辑、混音、重编排
4. **缺乏可控性** — 无法精确控制和弦、调式、乐器、节奏型

XiwuModel 采用 **MIDI 序列直接生成** 方案：
- 输出结果直接导入 DAW，每轨、每音符、每力度均可手动微调
- 支持指定调式、和声、BPM、拍号、风格、乐器等条件输入
- 4.5 亿参数即可达到专业级旋律/和声/鼓点质量

### 与 Agent 工具链的集成

XiwuModel 不是孤立运行的 —— 它是 XiwuCLAW 工具链中的**核心生成引擎**：

```
[Agent 意图解析]
       |
       v
[条件提取] → 调式/和声/BPM/风格/段结构
       |
       v
[XiwuModel 推理] → 条件 → MIDI Token 流 → 结构化 MIDI
       |
       v
[后处理管道]
  ├── single_track_post: 音高修正、量化、力度平滑
  ├── breakdown_generate: 多轨拆分与编排
  └── harmony_detect: 生成结果和弦验证
       |
       v
[DAW 放置] → VST OSC / AbletonOSC 自动落轨
```

### 模型能力

| 能力 | 说明 |
|------|------|
| 旋律生成 | 指定调式 + 和声进行 → 生成主旋律/MIDI |
| 和声生成 | 指定根音/根音走向 → 生成伴奏和弦 MIDI |
| 鼓点生成 | 指定风格（Pop/Rock/Jazz/HipHop）→ 生成鼓 MIDI |
| 多轨编排 | 旋律 + 和声 + 鼓 → 自动分层编排 |
| 风格迁移 | 基于记忆库中的风格偏好调整输出 |
| 段落延续 | 给定 MIDI 上下文 → 继续生成后续段落 |

### 训练数据

- MIDI 格式的专业音乐作品数据集
- 涵盖流行、爵士、电子、摇滚、古典等主流风格
- 标注：和声进行、曲式结构、乐器编配、力度动态
- 数据增强：调式变换、节奏拉伸、织体重组

### 技术路线

| 阶段 | 目标 |
|------|------|
| **v0.1 基础版** | 旋律单轨生成，支持 C 大调 / A 小调 |
| **v0.2 条件生成** | 支持完整调式/和声/BPM/拍号条件输入 |
| **v0.3 多轨生成** | 一次生成旋律+和声+鼓三轨，自动对位 |
| **v0.4 风格迁移** | 基于用户偏好记忆的动态风格调整 |
| **v1.0 正式版** | 集成 Hermes 自主 Agent，全流程语音控制 |

---

## 构建

```bash
# VST3 插件（需要 JUCE）
cd juce-vst3-gain
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Rust 主程序
cargo build --release
```

## 部署结构

```
H:\xiwuclawALL\
├── gateway\              # 主程序
│   └── xiwuclaw.exe
├── music_tools\          # Python 音乐工具
│   └── music_tools_manager.py
└── _python_runtime\      # 独立 Python 环境
    └── python.exe
```

## 许可证

MIT License
