<#
    一键构建脚本。

    它负责把三样东西准备好再调用 cmake：
      1. MSVC 编译环境（Visual Studio Build Tools 的 vcvars64.bat）
      2. LLVM（默认用 conda 环境 E:\Anaconda\envs\tinycompiler 里的 llvmdev 22.1.8）
      3. flex / bison（默认用 E:\Tools\winflexbison）

    用法:
        pwsh -File build.ps1                 # 配置 + 编译
        pwsh -File build.ps1 -Test           # 编译后再跑一遍测试用例
        pwsh -File build.ps1 -Clean          # 先清空构建目录

    路径都可以用参数覆盖:
        pwsh -File build.ps1 -LlvmDir E:\LLVM -FlexBisonDir E:\Tools\winflexbison
#>
param(
    [string]$BuildDir = "build",
    [string]$BuildType = "Release",
    [string]$LlvmDir = "E:\Anaconda\envs\tinycompiler\Library",
    [string]$FlexBisonDir = "E:\Tools\winflexbison",
    [string]$Vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    [switch]$Test,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $root $BuildDir }

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

# ---------------------------------------------------------------- LLVM 环境
$llvmConfig = Join-Path $LlvmDir "bin\llvm-config.exe"
if (-not (Test-Path $llvmConfig)) {
    Write-Error "找不到 LLVM: $llvmConfig`n请用 -LlvmDir 指定包含 bin\llvm-config.exe 的目录"
    exit 1
}
Write-Host "使用 LLVM $(& $llvmConfig --version) ($LlvmDir)"

# ---------------------------------------------------------------- flex/bison
$flex = Join-Path $FlexBisonDir "win_flex.exe"
$bison = Join-Path $FlexBisonDir "win_bison.exe"
if (-not (Test-Path $flex) -or -not (Test-Path $bison)) {
    Write-Error "找不到 flex/bison ($FlexBisonDir)`n请用 -FlexBisonDir 指定包含 win_flex.exe 和 win_bison.exe 的目录"
    exit 1
}
Write-Host "使用 flex $flex"
Write-Host "使用 bison $bison"

# ---------------------------------------------------------------- cmake
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "清理 $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

$configureArgs = @(
    "-S", $root,
    "-B", $BuildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    # LLVM 目录同时也是 zlib / zstd 的所在处，告诉 CMake 去那里找依赖库
    "-DCMAKE_PREFIX_PATH=$LlvmDir",
    "-DLLVM_DIR=$(Join-Path $LlvmDir 'lib\cmake\llvm')",
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
    & pwsh -File (Join-Path $root "tests\run_tests.ps1") -BuildDir $BuildDir
    exit $LASTEXITCODE
}
