# XiwuCLAW Probe 插件 - 验证说明

## 构建步骤

### Windows (MSVC)

```bash
cd h:\xiwuclaw-rs-standalone\juce-vst3-gain
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

构建产物：
- `XiwuCLAW.vst3` - 主插件（Instrument）
- `XiwuCLAW Probe.vst3` - Probe 插件（MIDI Effect）

---

## 验证步骤

### 1. 验证主插件启动并创建 Session 文件

1. 在 DAW（如 Reaper、Cubase、Ableton Live）中加载 `XiwuCLAW.vst3`
2. 检查 Session 文件是否创建：
   ```
   %APPDATA%\XiwuCLAW\probe_session.json
   ```
3. 文件内容应类似：
   ```json
   {
     "ok": true,
     "pipe_name": "XiwuCLAW.Probe.admin.12345.{uuid}",
     "host_pid": 12345,
     "created_ms": 1234567890.0,
     "created_iso": "2026-04-20T10:00:00"
   }
   ```

### 2. 验证 Probe 插件连接

1. 在同一 DAW 工程中，在 MIDI 轨道上加载 `XiwuCLAW Probe.vst3`
2. 打开 Probe 插件 UI
3. 观察状态显示：
   - **橙色 "Connecting..."** - 正在尝试连接主插件
   - **绿色 "Connected"** - 已成功连接到主插件
4. 修改 "Track Tag" 输入框（如 `Vocal`、`Piano` 等）

### 3. 验证 Probe Online 状态（OSC 接口）

主插件提供 OSC 接口查询在线 Probe：

**方法 A: 使用 OSC 客户端工具**
```bash
# 发送 OSC 消息到 localhost:3921
/xiwu/vst/track/active/snapshot/get
```

**方法 B: 使用 Rust  companion 应用**
```bash
# 如果有 xiwuclaw-agent 运行
cargo run --bin xiwuclaw-agent
```

**预期响应 JSON：**
```json
{
  "ok": true,
  "active_track_tag": "Vocal",
  "probe_online_count": 1,
  "probes": [
    {
      "track_tag": "Vocal",
      "probe_id": "{uuid}",
      "timestamp_ms": 1234567890,
      "midi_activity": 5,
      "note_density": 0.3,
      "audio_peak": 0.0,
      "age_ms": 150,
      "transport_hint": {
        "bar": 1,
        "beat": 1,
        "ppq": 0.0
      }
    }
  ],
  "bpm": 120.0,
  "beats_per_bar": 4,
  "beat_unit": 4,
  "time_signature": { "numerator": 4, "denominator": 4 },
  "playhead_beats": 0.0,
  "playhead_bar": 1,
  "playhead_beat": 1
}
```

### 4. 验证 MIDI 活动统计

1. 在加载 Probe 插件的 MIDI 轨道上播放 MIDI 音符
2. 观察 Probe 插件 UI 状态（应显示 Connected）
3. 通过 OSC 接口查询，`midi_activity` 和 `note_density` 应随 MIDI 输入变化

### 5. 验证 TTL 过期清理

1. 移除或禁用 Probe 插件
2. 等待 3 秒后查询 OSC 接口
3. `probe_online_count` 应变为 0，`probes` 数组应为空

---

## 故障排查

### Probe 插件显示 "Connecting..." 无法连接

1. 确认主插件已先于 Probe 插件加载
2. 检查 Session 文件是否存在：`%APPDATA%\XiwuCLAW\probe_session.json`
3. 检查 pipe 名是否正确（无特殊字符）
4. 尝试重启 DAW

### 多实例支持

系统支持多个主机实例（多个 DAW 或同一 DAW 多个主插件实例）：
- 每个主插件创建唯一的 pipe 名（含 PID + GUID）
- Probe 插件会扫描所有 session 文件并尝试连接
- 每个 Probe 只连接一个主插件

### 防火墙/杀毒软件

NamedPipe 是本地进程间通讯，不经过网络栈，通常不受防火墙影响。

---

## 文件结构

```
juce-vst3-gain/
├── src/                          # 主插件源码
│   ├── PluginProcessor.h         # 含 ProbePipeBridge, ProbeState
│   ├── PluginProcessor.cpp       # NamedPipe Server 实现
│   ├── PluginEditor.h
│   └── PluginEditor.cpp
├── src-probe/                    # Probe 插件源码
│   ├── PluginProcessor.h         # 含 ProbePipeClient, MidiActivityTracker
│   ├── PluginProcessor.cpp       # NamedPipe Client 实现
│   ├── PluginEditor.h
│   └── PluginEditor.cpp
├── CMakeLists.txt                # 含 GainPlugin 和 ProbePlugin 两个目标
└── tones/factory/                # 音色配置 JSON
```

---

## 技术细节

### Pipe 命名规则
```
XiwuCLAW.Probe.<UserSID>.<HostPID>.<SessionGUID>
```
- `UserSID`: 登录用户名（避免跨用户冲突）
- `HostPID`: 宿主进程 ID（避免同机多实例冲突）
- `SessionGUID`: 随机 GUID（确保唯一性）

### 心跳机制
- Probe 发送间隔：150ms
- 主插件 TTL：3000ms（3 秒无心跳视为离线）

### JSON 消息格式
```json
{
  "probe_id": "{uuid}",
  "track_tag": "UserDefined",
  "timestamp_ms": 1234567890,
  "midi_activity": 5,
  "note_density": 0.3,
  "audio_peak": 0.0
}
```
