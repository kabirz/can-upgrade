# 构建脚本 —— 封装 go build / go clean，产出无控制台 GUI 版 can-walk.exe。
# 用法: ./build.ps1 [build|run|clean|syso]   默认 build
param([Parameter(Position = 0)][string]$Action = "build")
$ErrorActionPreference = "Stop"

$Ldflags = "-H windowsgui -s -w" # GUI 子系统 + 去符号

switch ($Action) {
    "build" {
        go build -ldflags="$Ldflags"
        Write-Host "已构建 can-walk.exe" -ForegroundColor Green
    }
    "run" {
        go build -ldflags="$Ldflags"
        & ".\can-walk.exe"
    }
    "clean" {
        go clean
        Remove-Item -Force "can-walk.exe", "can-upgrade.exe" -ErrorAction SilentlyContinue
        Write-Host "已清理构建产物" -ForegroundColor Green
    }
    "syso" {
        # rsrc.syso 已随仓库提交，仅在改动 manifest.xml/icon.ico 后执行
        $rsrc = "$env:GOPATH\bin\rsrc.exe"
        if (-not (Test-Path $rsrc)) { go install github.com/akavel/rsrc@latest }
        & $rsrc -manifest manifest.xml -ico icon.ico -o rsrc.syso
        Write-Host "已重新生成 rsrc.syso" -ForegroundColor Green
    }
    default {
        Write-Host "用法: ./build.ps1 [build|run|clean|syso]" -ForegroundColor Yellow
    }
}
