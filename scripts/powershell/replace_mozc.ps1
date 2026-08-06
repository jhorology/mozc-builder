param(
  [string]$mozcDir,
  [string]$distDir
)

$ErrorActionPreference = 'Continue'

# Auto-detect $mozcDir if not provided
if (-not $mozcDir) {
  foreach ($dir in @('C:\Program Files (x86)\Mozc', 'C:\Program Files\Mozc')) {
    if (Test-Path (Join-Path $dir "mozc_server.exe")) {
      $mozcDir = $dir
      break
    }
  }
}

# Auto-detect $distDir if not provided
if (-not $distDir) {
  if (Test-Path (Join-Path $PSScriptRoot "mozc_server.exe")) {
    $distDir = $PSScriptRoot
  } elseif (Test-Path (Join-Path $PSScriptRoot "dist\mozc_server.exe")) {
    $distDir = Join-Path $PSScriptRoot "dist"
  } elseif (Test-Path ".\mozc_server.exe") {
    $distDir = (Get-Location).Path
  } elseif (Test-Path ".\dist\mozc_server.exe") {
    $distDir = (Get-Item ".\dist").FullName
  }
}

if (-not $mozcDir) {
  Write-Error "Could not auto-detect Mozc installation directory. Please specify -mozcDir parameter."
  exit 1
}

if (-not $distDir) {
  Write-Error "Could not auto-detect dist directory containing build binaries. Please specify -distDir parameter."
  exit 1
}

Write-Host "Mozc Install Dir : $mozcDir"
Write-Host "Build Dist Dir   : $distDir"

# 1. Stop MozcCacheService
Write-Host '[1/6] Stopping MozcCacheService...'
Stop-Service -Name MozcCacheService -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

# 2. Kill running Mozc processes
Write-Host '[2/6] Killing Mozc processes...'
foreach ($proc in @('mozc_server','mozc_renderer','mozc_broker','mozc_cache_service','mozc_tool')) {
  Get-Process -Name $proc -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 800

# 3. Copy EXE binaries (not TSF-locked)
Write-Host '[3/6] Copying EXE binaries...'
foreach ($f in @('mozc_server.exe','mozc_cache_service.exe','mozc_broker.exe','mozc_renderer.exe','mozc_tool.exe')) {
  $src = Join-Path $distDir $f
  $dst = Join-Path $mozcDir $f
  if (Test-Path $src) {
    Copy-Item -Path $src -Destination $dst -Force -ErrorAction SilentlyContinue
    Write-Host "  copied: $f"
  }
}

# 4. Copy TIP DLLs - stop TextInputManagementService to release TSF lock first
Write-Host '[4/6] Copying TIP DLLs (stopping TSF service to release lock)...'
$tsf = Get-Service -Name TextInputManagementService -ErrorAction SilentlyContinue
if ($tsf) { Stop-Service -Name TextInputManagementService -Force -ErrorAction SilentlyContinue }
Start-Sleep -Milliseconds 800
$pendingReplace = @()
foreach ($f in @('mozc_tip32.dll','mozc_tip64.dll')) {
  $src = Join-Path $distDir $f
  $dst = Join-Path $mozcDir $f
  if (Test-Path $src) {
    try {
      Copy-Item -Path $src -Destination $dst -Force -ErrorAction Stop
      Write-Host "  copied: $f"
    } catch {
      # Fallback: schedule replacement at next logon via MoveFileEx MOVEFILE_DELAY_UNTIL_REBOOT
      Write-Host "  locked: $f - scheduling replace at next logon..."
      $tmp = "$dst.new"
      Copy-Item -Path $src -Destination $tmp -Force -ErrorAction SilentlyContinue
      $sig = @'
[DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
public static extern bool MoveFileEx(string lpExistingFileName, string lpNewFileName, uint dwFlags);
'@
      $k32 = Add-Type -MemberDefinition $sig -Name K32 -Namespace Win32 -PassThru
      # MOVEFILE_REPLACE_EXISTING(1) | MOVEFILE_DELAY_UNTIL_REBOOT(4) = 5
      $k32::MoveFileEx($tmp, $dst, 5) | Out-Null
      $pendingReplace += $f
    }
  }
}
if ($tsf) { Start-Service -Name TextInputManagementService -ErrorAction SilentlyContinue }

# 5. Restart MozcCacheService
Write-Host '[5/6] Restarting MozcCacheService...'
Start-Service -Name MozcCacheService -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

# 6. Prelaunch Mozc
Write-Host '[6/6] Prelaunching Mozc processes...'
$broker = Join-Path $mozcDir 'mozc_broker.exe'
if (Test-Path $broker) {
  Start-Process -FilePath $broker -ArgumentList '--mode=prelaunch_processes' -WindowStyle Hidden
}

if ($pendingReplace.Count -gt 0) {
  Write-Host "Done. NOTE: $($pendingReplace -join ', ') will be replaced at next logon (TSF lock could not be released)."
} else {
  Write-Host 'Done. Mozc binaries updated without reboot.'
}
