<#
    一键构建脚本。

    它负责把三样东西准备好再调用 cmake：
      1. MSVC 编译环境（Visual Studio Build Tools 的 vcvars64.bat）
      2. LLVM（默认用官方发布的 clang+llvm-*-x86_64-pc-windows-msvc.tar.xz）
      3. flex / bison（默认用 E:\Tools\winflexbison）

    用法:
        pwsh -File build.ps1                 # 配置 + 编译
        pwsh -File build.ps1 -Test           # 编译后再跑一遍测试用例
        pwsh -File build.ps1 -Clean          # 先清空构建目录
        pwsh -File build.ps1 -LlvmDir D:\llvm # 指定 LLVM 目录

    LLVM 目录要满足下面两个条件（也就是「完整版」的 LLVM）：
        <目录>/include/llvm/IR/IRBuilder.h
        <目录>/lib/cmake/llvm/LLVMConfig.cmake
    官方那个 win64.exe 安装包不满足（它不含 C++ 头文件），详见 Readme。
#>
param(
    [string]$BuildDir = "build",
    [string]$BuildType = "Release",
    [string]$LlvmDir = "",
    [string]$FlexBisonDir = "E:\Tools\winflexbison",
    [string]$Vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    [switch]$Test,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $root $BuildDir }

function Test-LlvmDir([string]$dir) {
    if (-not $dir -or -not (Test-Path $dir)) { return $false }
    $hasHeader = Test-Path (Join-Path $dir "include\llvm\IR\IRBuilder.h")
    $hasConfig = Test-Path (Join-Path $dir "lib\cmake\llvm\LLVMConfig.cmake")
    return ($hasHeader -and $hasConfig)
}

function Find-LlvmDir {
    # 按优先级找一个可用的完整版 LLVM
    $candidates = @()
    if ($LlvmDir) { $candidates += $LlvmDir }
    # 1) 官方 tar.xz 解压出来的目录（E:\LLVM-full\clang+llvm-*-...）
    foreach ($root in @("E:\LLVM-full", "C:\LLVM-full", "$root\tools")) {
        if (Test-Path $root) {
            $candidates += (Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
                            Where-Object { $_.Name -like "clang+llvm-*" } |
                            Select-Object -ExpandProperty FullName)
        }
    }
    # 2) 常见的安装位置
    $candidates += @(
        "C:\Program Files\LLVM",
        "E:\LLVM",
        "$root\third_party\llvm"
    )
    # 3) conda 环境里的 llvmdev
    $condaRoot = (& conda info --base 2>$null)
    if ($condaRoot) {
        $candidates += (Join-Path $condaRoot "envs\tinycompiler\Library")
    }

    foreach ($c in $candidates) {
        if (Test-LlvmDir $c) { return $c }
    }
    return $null
}

# ---------------------------------------------------------------- LLVM 环境
$llvmDir = Find-LlvmDir
if (-not $llvmDir) {
    Write-Error "找不到完整版的 LLVM（需要 include\llvm 和 lib\cmake\llvm）。`n" +
                "请下载官方 tar.xz 包并解压，或用 -LlvmDir 指定目录。`n" +
                "  https://github.com/llvm/llvm-project/releases/tag/llvmorg-22.1.8"
    exit 1
}
$llvmConfig = Join-Path $llvmDir "bin\llvm-config.exe"
Write-Host "使用 LLVM $(& $llvmConfig --version) ($llvmDir)"

# ---------------------------------------------------------------- flex/bison
$flex = Join-Path $FlexBisonDir "win_flex.exe"
$bison = Join-Path $FlexBisonDir "win_bison.exe"
if (-not (Test-Path $flex) -or -not (Test-Path $bison)) {
    Write-Error "找不到 flex/bison ($FlexBisonDir)`n请用 -FlexBisonDir 指定包含 win_flex.exe 和 win_bison.exe 的目录"
    exit 1
}
Write-Host "使用 flex $flex"
Write-Host "使用 bison $bison"

# ---------------------------------------------------------------- MSVC 环境
if (Test-Path $Vcvars) {
    Write-Host "加载 MSVC 环境: $Vcvars"
    $dump = cmd /c "`"$Vcvars`" >nul 2>&1 && set"
    foreach ($line in $dump) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
        }
    }
} else {
    Write-Warning "找不到 vcvars64.bat ($Vcvars)，假设当前已经在 MSVC 开发者命令行里"
}

# MSBuild 会在临时目录里建 MSBuildTemp，如果临时目录的路径里含非 ASCII
# 字符（比如中文用户名），它会报「拒绝访问」。这时候换成一个纯英文路径。
$nonAscii = ($env:TEMP.ToCharArray() | Where-Object { [int]$_ -gt 127 }).Count -gt 0
if ($nonAscii) {
    $asciiTemp = Join-Path $BuildDir "tmp"
    New-Item -ItemType Directory -Force -Path $asciiTemp | Out-Null
    $env:TEMP = $asciiTemp
    $env:TMP = $asciiTemp
    Write-Host "临时目录路径含非 ASCII 字符，改用 $asciiTemp"
}

# ---------------------------------------------------------------- cmake
# MSBuild 在这个环境里没有记录头文件的依赖（CL.read.1.tlog 里只有 .cpp 没有 .h），
# 改了头文件不会触发重编，会用旧的结构体布局继续编，出来的程序行为诡异。
# 所以这里自己比较一下所有源文件（含头文件、语法文件）和可执行文件的时间戳：
# 只要有源文件更新了，就整体重建。
function Test-SourceChanged([string]$buildDir, [string]$buildType) {
    $exe = Join-Path $buildDir "$buildType\TinyCompiler.exe"
    if (-not (Test-Path $exe)) { return $true }
    $exeTime = (Get-Item $exe).LastWriteTime
    $newest = Get-ChildItem -Path $root -Recurse -File -ErrorAction SilentlyContinue |
              Where-Object { $_.Extension -in @('.cpp', '.h', '.y', '.l') } |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($null -eq $newest) { return $false }
    return $newest.LastWriteTime -gt $exeTime
}

if ($Clean -or (Test-SourceChanged $BuildDir $BuildType)) {
    if (-not $Clean) {
        Write-Host "源文件有改动，整体重建（MSBuild 不跟踪头文件依赖）"
    }
    Write-Host "清理 $BuildDir"
    Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
}

$configureArgs = @(
    "-S", $root,
    "-B", $BuildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    # LLVM 目录同时也是 zlib / zstd 的所在处，告诉 CMake 去那里找依赖库
    "-DCMAKE_PREFIX_PATH=$llvmDir",
    "-DLLVM_DIR=$(Join-Path $llvmDir 'lib\cmake\llvm')",
    "-DFLEX_EXECUTABLE=$flex",
    "-DBISON_EXECUTABLE=$bison"
)

Write-Host ""
Write-Host ">>> cmake configure"
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host ">>> cmake build"
& cmake --build $BuildDir --config $BuildType
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$compiler = Join-Path $BuildDir "TinyCompiler.exe"
if (-not (Test-Path $compiler)) {
    $compiler = Join-Path $BuildDir "$BuildType\TinyCompiler.exe"
}
Write-Host ""
Write-Host "编译完成: $compiler"

if ($Test) {
    Write-Host ""
    Write-Host ">>> 运行测试用例"
    & pwsh -File (Join-Path $root "tests\run_tests.ps1") -BuildDir $BuildDir -LlvmDir $llvmDir
    exit $LASTEXITCODE
}
