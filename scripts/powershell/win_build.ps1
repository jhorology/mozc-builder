param(
  [switch]$Emacs,
  [switch]$Install,
  [string]$WorkspaceDir
)

$ErrorActionPreference = 'Stop'

if (-not $WorkspaceDir) {
  if (Test-Path "$PSScriptRoot\mozc\src") {
    $WorkspaceDir = $PSScriptRoot
  } else {
    $WorkspaceDir = Get-Location
  }
}

$srcDir = Join-Path $WorkspaceDir "mozc\src"
$distDir = Join-Path $WorkspaceDir "dist"

if (-not (Test-Path $srcDir)) {
  Write-Error "Source directory not found: $srcDir"
  exit 1
}

# Clear Android SDK variables so Bazel's rules_android won't fail on Windows
$env:ANDROID_HOME = ""
$env:ANDROID_NDK_HOME = ""

# Prepare .bazelrc.user argument if present in WorkspaceDir
$bazelrcPath = Join-Path $WorkspaceDir ".bazelrc.user"
$bazelrcArgs = @()
if (Test-Path $bazelrcPath) {
  $bazelrcArgs += "--bazelrc=$bazelrcPath"
  $env:BAZELRC = $bazelrcPath
}

New-Item -ItemType Directory -Force -Path $distDir | Out-Null

Push-Location $srcDir
try {
  # 1. Bazel Build
  Write-Host "Start bazel build task..."
  $bazelTargets = @("package")
  if ($Emacs) {
    $bazelTargets += "//unix/emacs:mozc_emacs_helper"
  }

  $targetStr = $bazelTargets -join ' '
  Write-Host "Running: bazelisk $($bazelrcArgs -join ' ') build $targetStr --config oss_windows --config release_build"
  & bazelisk $bazelrcArgs build $bazelTargets --config oss_windows --config release_build --action_env=ANDROID_HOME= --action_env=ANDROID_NDK_HOME=
  if ($LASTEXITCODE -ne 0) {
    Write-Error "Bazel build failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
  }

  # 2. Get bazel-bin path
  $winBazelBin = (bazelisk $bazelrcArgs info bazel-bin --config oss_windows --config release_build 2>$null | Where-Object { $_ -match '^[A-Za-z]:' } | Select-Object -Last 1)
  if ($winBazelBin) {
    $winBazelBin = $winBazelBin.Trim()
    Write-Host "bazel-bin: $winBazelBin"
  } else {
    Write-Error "Could not resolve bazel-bin path"
    exit 1
  }

  # 3. Copy build artifacts to dist
  if ($Emacs) {
    $emacsHelper = Join-Path $winBazelBin "unix\emacs\mozc_emacs_helper.exe"
    if (Test-Path $emacsHelper) {
      Copy-Item -Path $emacsHelper -Destination $distDir -Force
      Write-Host "Copied mozc_emacs_helper.exe to dist"
    }
  }

  $msiFiles = Get-ChildItem -Path (Join-Path $winBazelBin "win32\installer") -Filter "*.msi" -ErrorAction SilentlyContinue
  foreach ($msi in $msiFiles) {
    Copy-Item -Path $msi.FullName -Destination $distDir -Force
    Write-Host "Copied $($msi.Name) to dist"
  }

  # 4. Extract binaries from MSI using msiexec /a
  $msiPath = Join-Path $distDir "Mozc64.msi"
  $extractDir = Join-Path $distDir "msi_extracted"

  if (Test-Path $msiPath) {
    Write-Host "Extracting binaries from MSI..."
    if (Test-Path $extractDir) {
      Remove-Item -Path $extractDir -Recurse -Force
    }

    Start-Process msiexec -ArgumentList '/a', "`"$msiPath`"", '/qn', "TARGETDIR=`"$extractDir`"" -Wait

    $extracted = Join-Path $extractDir "PFiles\Mozc"
    if (Test-Path $extracted) {
      $binaries = @('mozc_renderer.exe', 'mozc_server.exe', 'mozc_broker.exe', 'mozc_cache_service.exe', 'mozc_tool.exe', 'mozc_tip32.dll', 'mozc_tip64.dll')
      foreach ($bin in $binaries) {
        $binPath = Join-Path $extracted $bin
        if (Test-Path $binPath) {
          Copy-Item -Path $binPath -Destination $distDir -Force
        }
      }
      Write-Host "MSI extraction complete"
    } else {
      Write-Host "Warning: MSI extraction failed (dir not found: $extracted)."
    }
  }

  # 5. Install if requested
  if ($Install) {
    Write-Host "Installing Mozc..."
    if ($Emacs) {
      $userBin = Join-Path $env:USERPROFILE ".local\bin"
      New-Item -ItemType Directory -Force -Path $userBin | Out-Null
      $emacsExe = Join-Path $distDir "mozc_emacs_helper.exe"
      if (Test-Path $emacsExe) {
        Copy-Item -Path $emacsExe -Destination $userBin -Force
        Write-Host "Installed mozc_emacs_helper.exe to $userBin"
      }
    }

    # Get installed version
    $installedVer = ""
    $checkDirs = @(
      'C:\Program Files (x86)\Mozc',
      'C:\Program Files\Mozc',
      'C:\Program Files (x86)\Google\Mozc',
      'C:\Program Files\Google\Mozc'
    )
    foreach ($dir in $checkDirs) {
      $f = Join-Path $dir "mozc_server.exe"
      if (Test-Path $f) {
        $vi = (Get-Item $f).VersionInfo
        if ($vi.ProductVersion) {
          $installedVer = $vi.ProductVersion.Trim()
          break
        } elseif ($vi.FileVersion) {
          $installedVer = $vi.FileVersion.Trim()
          break
        }
      }
    }

    # Version check against version.bzl
    $bzlFile = Join-Path $srcDir "version.bzl"
    $major = ""; $minor = ""; $buildVal = ""; $revision = ""
    if (Test-Path $bzlFile) {
      $bzlLines = Get-Content $bzlFile
      foreach ($line in $bzlLines) {
        if ($line -match '^\s*MAJOR\s*=\s*(\d+)') { $major = $matches[1] }
        if ($line -match '^\s*MINOR\s*=\s*(\d+)') { $minor = $matches[1] }
        if ($line -match '^\s*BUILD\s*=\s*(\w+)') { $buildVal = $matches[1] }
        if ($line -match '^\s*REVISION\s*=\s*(\d+)') { $revision = $matches[1] }
      }
      if ($buildVal -and -not ($buildVal -match '^\d+$')) {
        foreach ($line in $bzlLines) {
          if ($line -match "^\s*$buildVal\s*=\s*(\d+)") {
            $buildVal = $matches[1]
            break
          }
        }
      }
    }

    if ($major) {
      $targetVer = "$major.$minor.$buildVal.$revision"
    } else {
      $targetVer = "3.34.6239.100"
    }

    $displayVer = if ($installedVer) { $installedVer } else { 'none' }
    if (-not $installedVer -or -not $installedVer.StartsWith($targetVer)) {
      Write-Host "Installing/upgrading Mozc via MSI (installed: '$displayVer', target: '$targetVer')..."
      Start-Process msiexec -ArgumentList '/i', "`"$msiPath`"", 'REINSTALL=ALL', 'REINSTALLMODE=amus' -Wait
      Write-Host "MSI installation finished"
    } else {
      Write-Host "Same version ($installedVer) detected. Directly overwriting binaries..."
      $mozcDir = ""
      foreach ($dir in @('C:\Program Files (x86)\Mozc', 'C:\Program Files\Mozc')) {
        if (Test-Path (Join-Path $dir "mozc_server.exe")) {
          $mozcDir = $dir
          break
        }
      }

      if (-not $mozcDir) {
        Write-Error "Could not find Mozc installation directory. Please install via MSI first."
      } else {
        Write-Host "Mozc install dir: $mozcDir"
        $replaceScript = Join-Path $PSScriptRoot "replace_mozc.ps1"
        if (-not (Test-Path $replaceScript)) {
          $replaceScript = Join-Path $distDir "replace_mozc.ps1"
        }

        if (Get-Command sudo -ErrorAction SilentlyContinue) {
          sudo powershell -NoProfile -ExecutionPolicy Bypass -File "$replaceScript" "$mozcDir" "$distDir"
        } else {
          Start-Process powershell -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$replaceScript`"", "`"$mozcDir`"", "`"$distDir`"" -Verb RunAs -Wait
        }
        Write-Host "Direct binary update finished"
      }
    }
  }
} finally {
  Pop-Location
}
