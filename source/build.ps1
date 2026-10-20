param(
    [string]$Toolchain = 'C:\msys64\ucrt64\bin',
    [string]$BuildDirectory = (Join-Path ([System.IO.Path]::GetTempPath()) ('CopyConfirm-build-' + $PID))
)
$ErrorActionPreference = 'Stop'
$taskCompiler = Join-Path $Toolchain 'g++.exe'
$taskResourceCompiler = Join-Path $Toolchain 'windres.exe'
if (!(Test-Path -LiteralPath $taskCompiler) -or !(Test-Path -LiteralPath $taskResourceCompiler)) {
    throw 'MSYS2 UCRT64 g++ and windres are required. No installation is performed by this script.'
}
New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
$taskBuildPath = (Resolve-Path -LiteralPath $BuildDirectory).Path
$taskOutput = Join-Path (Split-Path -Parent $PSScriptRoot) 'CopyConfirm.exe'
Push-Location -LiteralPath $PSScriptRoot
try {
    & $taskResourceCompiler '--codepage=65001' '-I.' 'CopyConfirm.rc' (Join-Path $taskBuildPath 'CopyConfirm.res.o')
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
    & $taskCompiler '-std=c++20' '-O2' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-Wno-missing-field-initializers' `
        '-municode' '-mwindows' '-static' '-static-libgcc' '-static-libstdc++' '-s' `
        '-finput-charset=UTF-8' '-fexec-charset=UTF-8' `
        'CopyConfirm.cpp' (Join-Path $taskBuildPath 'CopyConfirm.res.o') `
        '-luser32' '-lgdi32' '-lshell32' '-lgdiplus' '-lole32' '-lruntimeobject' '-luuid' '-ladvapi32' `
        '-o' $taskOutput
    if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
    Get-Item -LiteralPath $taskOutput | Select-Object FullName,Length
} finally {
    Pop-Location
}
