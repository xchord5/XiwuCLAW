# XiwuCLAW Probe NamedPipe 通信测试用例

## 前置准备

### 1. 确认插件文件位置
```
主插件：build-ninja/GainPlugin_artefacts/Debug/VST3/XiwuCLAW.vst3
Probe 插件：build-ninja/ProbePlugin_artefacts/Debug/VST3/XiwuCLAW Probe.vst3
```

### 2. 安装 VST3 插件到 DAW
将以下目录添加到 DAW 的 VST3 插件搜索路径：
```
H:\xiwuclaw-rs-standalone\juce-vst3-gain\build-ninja\GainPlugin_artefacts\Debug\VST3\
H:\xiwuclaw-rs-standalone\juce-vst3-gain\build-ninja\ProbePlugin_artefacts\Debug\VST3\
```

---

## 功能说明

### Probe 插件功能
- 监听所在轨道的 MIDI 消息
- 记录完整的 MIDI 音符历史（note_on/note_off）
- 接收主插件下发的时间窗口请求
- 返回指定时间窗口内的 MIDI 音符详情
- 导出 MIDI 文件到临时目录

### 主插件功能
- 创建 NamedPipe Server (`\\.\pipe\XiwuCLAW.Probe.<SID>.<PID>.<GUID>`)
- 写入 Session 文件 (`%APPDATA%\XiwuCLAW\probe_session.json`)
- 接收 Probe 心跳状态（TTL=3 秒自动过期）
- 下发时间窗口请求获取 MIDI 详情
- 在 UI 底部显示 Probe MIDI 信息

### 通信协议
**Probe → Host (心跳)**:
```json
{
  "probe_id": "<uuid>",
  "track_tag": "Vocal",
  "category": "Melody",
  "timestamp_ms": 1234567890,
  "midi_activity": 5,
  "note_density": 0.3,
  "audio_peak": 0.0
}
```

**Host → Probe (MIDI 详情请求)**:
```json
{
  "cmd": "get_midi_details",
  "window_start_ms": 0,
  "window_end_ms": 10000
}
```

**Probe → Host (MIDI 详情响应)**:
```json
{
  "probe_id": "<uuid>",
  "track_tag": "Vocal",
  "midi_details": {
    "window_start_ms": 0,
    "window_end_ms": 10000,
    "note_count": 15,
    "notes": [
      {"time_ms": 100, "note": 60, "velocity": 0.8, "on": true},
      {"time_ms": 200, "note": 64, "velocity": 0.7, "on": true},
      ...
    ]
  }
}
```

---

## 测试用例 1：基础 NamedPipe 连接测试

### 目标
验证 Probe 插件能够连接到主插件创建的 NamedPipe

### 步骤

1. **启动 DAW** (Reaper/Bitwig/Ableton Live 等)

2. **加载主插件 (XiwuCLAW)**
   - 在任意音轨上插入 `XiwuCLAW.vst3`
   - 观察日志输出，确认 pipe server 创建成功

3. **检查 Session 文件是否创建**
   ```powershell
   # 在 PowerShell 中运行
   Get-Content "$env:APPDATA\XiwuCLAW\probe_session.json" | ConvertFrom-Json
   ```
   
   预期输出：
   ```json
   {
     "pipe_name": "XiwuCLAW.Probe.<SID>.<PID>.<GUID>",
     "host_pid": 12345,
     "created_ms": 1234567890.0,
     "created_iso": "2026-04-20T10:00:00Z",
     "ok": true
   }
   ```

4. **加载 Probe 插件**
   - 在另一音轨插入 `XiwuCLAW Probe.vst3`
   - 打开 Probe 插件编辑器界面

5. **观察 Probe 插件 UI 状态**
   - 初始状态：`Status: Connecting...` (橙色文字)
   - 连接成功后：`Status: Connected | Track1` (绿色文字)

6. **切换 Track Tag**
   - 在 Probe 插件 UI 中选择不同的 `Track Tag` (如 "Vocal", "Piano")
   - 确认 UI 状态显示更新

### 预期结果
- [ ] Session 文件成功创建
- [ ] Probe 插件状态从 "Connecting..." 变为 "Connected"
- [ ] Track Tag 切换正常显示

---

## 测试用例 2：OSC 接口验证 Probe 在线状态

### 目标
通过 OSC 接口查询在线 Probe 列表

### 步骤

1. **确保主插件和至少一个 Probe 插件已加载**

2. **发送 OSC 查询请求**
   ```powershell
   # 使用 PowerShell 发送 OSC 请求
   $udp = New-Object System.Net.Sockets.UdpClient
   $udp.Connect("127.0.0.1", 3921)
   
   # OSC 消息格式：/track/active/snapshot/get
   $message = [System.Text.Encoding]::UTF8.GetBytes("/track/active/snapshot/get`0`0`0`0")
   $udp.Send($message, $message.Length)
   
   # 接收响应 (timeout 2 秒)
   $endpoint = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
   $udp.Client.ReceiveTimeout = 2000
   $response = $udp.Receive([ref]$endpoint)
   [System.Text.Encoding]::UTF8.GetString($response)
   ```

3. **或使用 netcat (如果有安装)**
   ```bash
   echo -ne "/track/active/snapshot/get\x00\x00\x00\x00" | nc -u 127.0.0.1 3921
   ```

### 预期响应 JSON
```json
{
  "ok": true,
  "probe_online_count": 1,
  "probes": [
    {
      "probe_id": "<uuid>",
      "track_tag": "Track1",
      "category": "Drums",
      "timestamp_ms": 1234567890,
      "midi_activity": 0,
      "note_density": 0.0,
      "audio_peak": 0.0,
      "last_seen_local_ms": 1234567890
    }
  ]
}
```

### 预期结果
- [ ] OSC 请求成功发送
- [ ] 收到包含 probe 信息的 JSON 响应
- [ ] `probe_online_count` >= 1
- [ ] `probes` 数组包含 Probe 插件信息

---

## 测试用例 3：MIDI 活动检测

### 目标
验证 Probe 插件能够检测 MIDI 活动并上报

### 步骤

1. **加载 Probe 插件** 到 MIDI 音轨

2. **发送 MIDI 音符**
   - 使用 DAW 的钢琴卷帘或 MIDI 键盘
   - 发送一串音符 (如 C4, E4, G4 和弦)

3. **观察 Probe 插件 UI**
   - 状态栏应显示更新的 Track Tag

4. **查询 OSC 接口** (同测试用例 2)
   - 检查 `midi_activity` 和 `note_density` 字段

### 预期响应
```json
{
  "ok": true,
  "probes": [
    {
      "probe_id": "<uuid>",
      "track_tag": "Vocal",
      "midi_activity": 3,
      "note_density": 0.6,
      ...
    }
  ]
}
```

### 预期结果
- [ ] MIDI 音符被 Probe 插件检测
- [ ] `midi_activity` 显示非零值
- [ ] `note_density` 在 0.0-1.0 范围内

---

## 测试用例 4：多 Probe 并发连接

### 目标
验证多个 Probe 插件可同时连接

### 步骤

1. **加载主插件** (XiwuCLAW.vst3)

2. **加载 3 个 Probe 插件** 到不同音轨
   - Probe 1: Track Tag = "Vocal"
   - Probe 2: Track Tag = "Piano"
   - Probe 3: Track Tag = "Drums"

3. **查询 OSC 接口**

### 预期响应
```json
{
  "ok": true,
  "probe_online_count": 3,
  "probes": [
    {"track_tag": "Vocal", ...},
    {"track_tag": "Piano", ...},
    {"track_tag": "Drums", ...}
  ]
}
```

### 预期结果
- [ ] 所有 3 个 Probe 插件都显示为已连接
- [ ] `probe_online_count` = 3
- [ ] 每个 Probe 保持独立的 track_tag

---

## 测试用例 5：TTL 过期清理

### 目标
验证 Probe 状态在 3 秒无更新后自动过期

### 步骤

1. **加载主插件 + 1 个 Probe 插件**

2. **确认 Probe 在线** (通过 OSC 查询)

3. **禁用 Probe 插件**
   - 在 DAW 中 bypass 或移除 Probe 插件

4. **等待 4 秒**

5. **再次查询 OSC 接口**

### 预期响应
```json
{
  "ok": true,
  "probe_online_count": 0,
  "probes": []
}
```

### 预期结果
- [ ] Probe 禁用后 3 秒内从 map 中移除
- [ ] `probe_online_count` 变为 0

---

## 故障排查

### Probe 一直显示 "Connecting..."

1. 检查 Session 文件是否存在：
   ```powershell
   Test-Path "$env:APPDATA\XiwuCLAW\probe_session.json"
   ```

2. 检查 pipe 名称格式：
   ```powershell
   (Get-Content "$env:APPDATA\XiwuCLAW\probe_session.json" | ConvertFrom-Json).pipe_name
   ```

3. 使用 Process Monitor 检查 NamedPipe 活动：
   - 下载 [Process Monitor](https://learn.microsoft.com/en-us/sysinternals/downloads/procmon)
   - 过滤 `PipeName` 包含 `XiwuCLAW`

### OSC 无响应

1. 确认主插件已正确加载
2. 检查端口 3921 是否被占用：
   ```powershell
   netstat -ano | findstr :3921
   ```
3. 查看主插件日志输出

---

## 快速测试脚本

```powershell
# test-probe.ps1
# 使用示例：.\test-probe.ps1 -TrackTag "Vocal"

param(
    [string]$TrackTag = "Track1"
)

$sessionFile = "$env:APPDATA\XiwuCLAW\probe_session.json"

Write-Host "=== XiwuCLAW Probe 测试 ===" -ForegroundColor Cyan

# 检查 Session 文件
if (Test-Path $sessionFile) {
    Write-Host "[OK] Session 文件存在" -ForegroundColor Green
    $session = Get-Content $sessionFile | ConvertFrom-Json
    Write-Host "  Pipe 名称：$($session.pipe_name)"
    Write-Host "  Host PID: $($session.host_pid)"
} else {
    Write-Host "[FAIL] Session 文件不存在 - 请先加载主插件" -ForegroundColor Red
    exit 1
}

# 发送 OSC 查询
Write-Host "`n[测试] 发送 OSC 查询..." -ForegroundColor Yellow
$udp = New-Object System.Net.Sockets.UdpClient
$udp.Connect("127.0.0.1", 3921)
$message = [System.Text.Encoding]::UTF8.GetBytes("/track/active/snapshot/get`0`0`0`0")
$udp.Send($message, $message.Length)

$endpoint = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
$udp.Client.ReceiveTimeout = 2000

try {
    $response = $udp.Receive([ref]$endpoint)
    $json = [System.Text.Encoding]::UTF8.GetString($response)
    $data = $json | ConvertFrom-Json
    
    Write-Host "[OK] 收到响应:" -ForegroundColor Green
    Write-Host "  Probe 在线数：$($data.probe_online_count)"
    
    if ($data.probes -and $data.probes.Count -gt 0) {
        foreach ($probe in $data.probes) {
            Write-Host "  - $($probe.track_tag) (MIDI: $($probe.midi_activity))"
        }
    }
} catch {
    Write-Host "[FAIL] 未收到响应 - 确认主插件已加载" -ForegroundColor Red
}

$udp.Close()
```

---

## 通过标准

| 测试项 | 通过条件 |
|--------|----------|
| 基础连接 | Session 文件创建 + Probe 显示 Connected |
| OSC 查询 | 返回有效 JSON 且 probe_online_count >= 1 |
| MIDI 检测 | midi_activity > 0 |
| 多 Probe | probe_online_count = 加载的 Probe 数量 |
| TTL 过期 | 禁用后 4 秒内 probe_online_count = 0 |
