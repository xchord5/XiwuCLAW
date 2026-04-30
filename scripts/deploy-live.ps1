param(
  [string]$Source = "$PSScriptRoot\..\build\GainPlugin_artefacts\Release\VST3\XiwuCLAW.vst3\Contents\x86_64-win\XiwuCLAW.vst3",
  [string]$Target = "D:\2026 rust pj\vst3test\XiwuCLAW.vst3",
  [switch]$NoBackup
)

$ErrorActionPreference = "Stop"

function Resolve-StrictPath {
  param([Parameter(Mandatory = $true)][string]$Path)
  $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
  return $resolved.Path
}

$sourcePath = Resolve-StrictPath -Path $Source
$targetDir = Split-Path -Parent $Target
if ([string]::IsNullOrWhiteSpace($targetDir)) {
  throw "Invalid target path: $Target"
}

if (-not (Test-Path -LiteralPath $targetDir)) {
  New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
}

$targetPath = $Target
if (Test-Path -LiteralPath $targetPath) {
  if (-not $NoBackup) {
    $backupPath = "$targetPath.bak_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
    Copy-Item -LiteralPath $targetPath -Destination $backupPath -Force
    Write-Host "Backup created: $backupPath"
  }
}

$copySucceeded = $false
for ($i = 1; $i -le 5; $i++) {
  try {
    Copy-Item -LiteralPath $sourcePath -Destination $targetPath -Force
    $copySucceeded = $true
    break
  } catch {
    if ($i -lt 5) {
      Start-Sleep -Milliseconds 400
    } else {
      throw "Deploy failed: target is locked. Close Ableton Live and PluginScanner, then run again.`nTarget: $targetPath"
    }
  }
}

if (-not $copySucceeded) {
  throw "Deploy failed: could not copy plugin to target."
}

$sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourcePath).Hash
$targetHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $targetPath).Hash

Write-Host "Source : $sourcePath"
Write-Host "Target : $targetPath"
Write-Host "SHA256 : $targetHash"

if ($sourceHash -ne $targetHash) {
  throw "Deploy failed: hash mismatch between source and target."
}

Write-Host "Deploy success. Please rescan plugins in Live 11."
