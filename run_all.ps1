# SafeVault FS - Full-Stack Launcher
# Run from the project root:
#   cd D:\3rd_year\OS\EventDrivenFileSystem
#   .\run_all.ps1

$Root        = $PSScriptRoot
$CppExe      = "$Root\safevault_cpp\build\safevault.exe"
$WatchDir    = "$Root\watched"
$FrontendDir = "$Root\frontend"
$DbPath      = "$Root\safevault_cpp\.safevault_storage\safevault.sqlite3"

Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  SafeVault FS - Full-Stack Launcher" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# Pre-flight: check C++ binary
if (-not (Test-Path $CppExe)) {
    Write-Host "[ERROR] C++ binary not found at:" -ForegroundColor Red
    Write-Host "        $CppExe" -ForegroundColor Red
    Write-Host ""
    Write-Host "  Build it first (from safevault_cpp\):" -ForegroundColor Yellow
    Write-Host "    cmake -B build -G NMakeFiles" -ForegroundColor Yellow
    Write-Host "    cmake --build build" -ForegroundColor Yellow
    exit 1
}

# Pre-flight: check npm
if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
    Write-Host "[ERROR] npm not found. Install Node.js from https://nodejs.org" -ForegroundColor Red
    exit 1
}

# Create watched dir if missing
if (-not (Test-Path $WatchDir)) {
    New-Item -ItemType Directory -Path $WatchDir | Out-Null
    Write-Host "[INFO] Created watched directory: $WatchDir" -ForegroundColor Green
}

# Init the database
Write-Host "[INIT] Initializing SafeVault database..." -ForegroundColor DarkCyan
& $CppExe --database $DbPath init 2>&1 | Out-Null

# Install npm deps on first run
if (-not (Test-Path "$FrontendDir\node_modules")) {
    Write-Host "[SETUP] Installing frontend npm packages..." -ForegroundColor Yellow
    Push-Location $FrontendDir
    npm install --silent
    Pop-Location
}

# Helper: open a new terminal window
$wtAvailable = $null -ne (Get-Command wt -ErrorAction SilentlyContinue)

function Start-SafeVaultWindow {
    param([string]$Title, [string]$WorkDir, [string]$Command)
    if ($wtAvailable) {
        Start-Process wt -ArgumentList "new-tab --title `"$Title`" --startingDirectory `"$WorkDir`" cmd /k `"$Command`""
    } else {
        Start-Process cmd -ArgumentList "/k title $Title && cd /d `"$WorkDir`" && $Command"
    }
}

# Window 1: C++ Watcher
Write-Host ""
Write-Host "[1/3] Starting C++ Watcher..." -ForegroundColor Green
Write-Host "      Dir : $WatchDir" -ForegroundColor Gray
$watchCmd = "`"$CppExe`" --database `"$DbPath`" watch `"$WatchDir`""
Start-SafeVaultWindow -Title "SafeVault Watcher" -WorkDir $Root -Command $watchCmd
Start-Sleep -Milliseconds 800

# Window 2: C++ API Server
Write-Host "[2/3] Starting C++ AI API Server on port 8787..." -ForegroundColor Green
$serveCmd = "`"$CppExe`" --database `"$DbPath`" serve --bind 127.0.0.1:8787"
Start-SafeVaultWindow -Title "SafeVault API Server 8787" -WorkDir $Root -Command $serveCmd
Start-Sleep -Milliseconds 800

# Window 3: React Frontend
Write-Host "[3/3] Starting React Frontend on port 5173..." -ForegroundColor Green
Start-SafeVaultWindow -Title "SafeVault Frontend 5173" -WorkDir $FrontendDir -Command "npm run dev"

# Dashboard
Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  All services launched!" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Watcher    " -NoNewline; Write-Host "watching $WatchDir" -ForegroundColor Yellow
Write-Host "  API Server " -NoNewline; Write-Host "http://127.0.0.1:8787/api/parse" -ForegroundColor Yellow
Write-Host "  Frontend   " -NoNewline; Write-Host "http://localhost:5173" -ForegroundColor Yellow
Write-Host ""
Write-Host "  Open http://localhost:5173 in your browser" -ForegroundColor Cyan
Write-Host "  Click 'Choose folder' and select: $WatchDir" -ForegroundColor Cyan
Write-Host ""
Write-Host "  CLI shortcuts:" -ForegroundColor DarkGray
Write-Host "    $CppExe history" -ForegroundColor DarkGray
Write-Host "    $CppExe search 'query' --root '$WatchDir'" -ForegroundColor DarkGray
Write-Host ""
Write-Host "  Press Ctrl+C here to exit the launcher." -ForegroundColor Red
Write-Host ""

# Keep alive with heartbeat
try {
    $lastPing = [DateTime]::Now
    while ($true) {
        Start-Sleep -Seconds 5
        if (([DateTime]::Now - $lastPing).TotalSeconds -ge 60) {
            Write-Host "  [$(Get-Date -Format 'HH:mm:ss')] SafeVault running..." -ForegroundColor DarkGray
            $lastPing = [DateTime]::Now
        }
    }
} finally {
    Write-Host ""
    Write-Host "[STOP] Launcher exited. Close the other windows manually." -ForegroundColor Red
}