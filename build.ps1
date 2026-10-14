param([string]$Compiler)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
if (!$Compiler) {
    $command = Get-Command zig.exe -ErrorAction SilentlyContinue
    $Compiler = if ($command) { $command.Source } else { Join-Path $env:LOCALAPPDATA 'Programs\Zig\zig.exe' }
}
if (!(Test-Path -LiteralPath $Compiler)) { throw '未找到 Zig 编译器。请加入 PATH，或使用 -Compiler 指定 zig.exe。' }
& $Compiler c++ main.cpp glass.cpp tear.cpp book.cpp -std=c++20 -Wall -Wextra -Werror -O2 -g0 -municode '-Wl,--subsystem,windows' -luser32 -lgdi32 -lcomctl32 -ldwmapi -lshell32 -lole32 -ld3d11 -ldxgi -lmsimg32 -ladvapi32 -o LightNotes.exe
if ($LASTEXITCODE -ne 0) { throw '编译失败' }
Write-Host '已生成 LightNotes.exe'
