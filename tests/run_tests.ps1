<#
    端到端测试：编译 -> 链接 -> 运行 -> 比对输出

    用法:
        pwsh -File tests/run_tests.ps1
        pwsh -File tests/run_tests.ps1 -BuildDir build -UpdateExpected

    每个用例由下面这些文件组成：
        tests/xxx.input      源代码
        tests/xxx.expected   程序的预期输出（没有就只做编译检查）
        tests/xxx.stdin      喂给程序的输入（可选）
#>
param(
    [string]$BuildDir = "build",
    [string]$LlvmDir = "",
    [switch]$UpdateExpected
)

$ErrorActionPreference = "Continue"

$root = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $root $BuildDir
}

function Find-Executable {
    param([string[]]$Names, [string[]]$ExtraDirs = @())
    foreach ($name in $Names) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    foreach ($dir in $ExtraDirs) {
        foreach ($name in $Names) {
            $candidate = Join-Path $dir $name
            if (Test-Path $candidate) { return $candidate }
        }
    }
    return $null
}

function Find-LegacyStdio {
    # MSVC 的运行库里，scanf / fopen 这些老名字放在单独的库中，
    # 我们生成的目标文件直接引用 scanf，所以链接时要带上它。
    $vsRoot = "C:\Program Files (x86)\Microsoft Visual Studio\2022"
    if (-not (Test-Path $vsRoot)) { return $null }
    $found = Get-ChildItem $vsRoot -Recurse -Filter 'legacy_stdio_definitions.lib' -ErrorAction SilentlyContinue |
             Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1
    if ($found) { return $found.FullName }
    return $null
}

# ---------------------------------------------------------------- 找工具

$compiler = Find-Executable -Names @("TinyCompiler.exe", "TinyCompiler") -ExtraDirs @(
    $BuildDir,
    (Join-Path $BuildDir "Release"),
    (Join-Path $BuildDir "Debug")
)
if (-not $compiler) {
    Write-Host "找不到 TinyCompiler.exe，请先执行： pwsh -File build.ps1" -ForegroundColor Red
    exit 1
}

function Find-LlvmDir {
    # 找一份完整版 LLVM（要有 bin\clang.exe），用来链接测试程序
    $candidates = @()
    if ($LlvmDir) { $candidates += $LlvmDir }
    foreach ($r in @("E:\LLVM-full", "C:\LLVM-full")) {
        if (Test-Path $r) {
            $candidates += (Get-ChildItem $r -Directory -ErrorAction SilentlyContinue |
                            Where-Object { $_.Name -like "clang+llvm-*" } |
                            Select-Object -ExpandProperty FullName)
        }
    }
    $candidates += @("C:\Program Files\LLVM", "E:\LLVM")
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c "bin\clang.exe")) { return $c }
    }
    return ""
}

$LlvmDir = Find-LlvmDir

$linker = Find-Executable -Names @("clang.exe", "clang") -ExtraDirs @(
    (Join-Path $LlvmDir "bin"),
    "E:\LLVM-full\clang+llvm-22.1.8-x86_64-pc-windows-msvc\bin",
    "E:\LLVM\bin"
)
if (-not $linker) {
    $linker = Find-Executable -Names @("g++.exe", "g++")
}
if (-not $linker) {
    Write-Host "找不到链接器（clang 或 g++）" -ForegroundColor Red
    exit 1
}

$legacyStdio = Find-LegacyStdio

Write-Host "编译器: $compiler"
Write-Host "链接器: $linker"
Write-Host ""

$work = Join-Path $BuildDir "testrun"
New-Item -ItemType Directory -Force -Path $work | Out-Null

# ---------------------------------------------------------------- 跑用例

$passed = 0
$failed = 0
$skipped = 0

$inputs = Get-ChildItem $PSScriptRoot -Filter "*.input" | Sort-Object Name
foreach ($input in $inputs) {
    $name = $input.BaseName
    $obj = Join-Path $work "$name.obj"
    $exe = Join-Path $work "$name.exe"
    $ll = Join-Path $work "$name.ll"

    # 1) 生成 LLVM IR
    & $compiler -S -o $ll $input.FullName *> $null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] $name : 生成 IR 失败" -ForegroundColor Red
        & $compiler -S -o $ll $input.FullName
        $failed++
        continue
    }

    # 2) 生成目标文件
    & $compiler -o $obj $input.FullName *> $null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] $name : 生成目标文件失败" -ForegroundColor Red
        & $compiler -o $obj $input.FullName
        $failed++
        continue
    }

    # 3) 链接成可执行文件
    $linkArgs = @($obj, "-o", $exe)
    if ($legacyStdio) { $linkArgs += $legacyStdio }
    # 这个用例本身没有 main，需要额外提供一个入口
    $stub = Join-Path $PSScriptRoot "$name`_main.cpp"
    if (Test-Path $stub) {
        $linkArgs += $stub
    }
    & $linker @linkArgs *> $null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] $name : 链接失败" -ForegroundColor Red
        & $linker @linkArgs
        $failed++
        continue
    }

    # 4) 运行并比对输出
    $stdinFile = Join-Path $PSScriptRoot "$name.stdin"
    if (Test-Path $stdinFile) {
        $actual = (Get-Content $stdinFile -Raw | & $exe 2>&1 | Out-String)
    } else {
        $actual = (& $exe 2>&1 | Out-String)
    }

    $expectedFile = Join-Path $PSScriptRoot "$name.expected"

    if ($UpdateExpected) {
        Set-Content -Path $expectedFile -Value $actual -NoNewline
        Write-Host "[SAVE] $name : 已更新 $name.expected" -ForegroundColor Yellow
        $skipped++
        continue
    }

    if (-not (Test-Path $expectedFile)) {
        Write-Host "[SKIP] $name : 缺少 $name.expected，只做了编译检查" -ForegroundColor DarkGray
        $skipped++
        continue
    }

    $expected = Get-Content $expectedFile -Raw
    if ($actual.TrimEnd() -eq $expected.TrimEnd()) {
        Write-Host "[PASS] $name" -ForegroundColor Green
        $passed++
    } else {
        Write-Host "[FAIL] $name : 输出和预期不一致" -ForegroundColor Red
        Write-Host "  预期: $($expected.TrimEnd())"
        Write-Host "  实际: $($actual.TrimEnd())"
        $failed++
    }
}

Write-Host ""
Write-Host "通过 $passed 个，失败 $failed 个，跳过 $skipped 个"
exit $(if ($failed -gt 0) { 1 } else { 0 })
