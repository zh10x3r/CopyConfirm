param(
    [string]$BuildDirectory = (Join-Path ([System.IO.Path]::GetTempPath()) ('CopyConfirm-build-' + $PID))
)
$ErrorActionPreference = 'Stop'
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $taskVswhere)) { throw 'Install MSYS2 UCRT64 or Visual Studio with the C++ desktop tools before building.' }
$taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$taskVs) { throw 'No installed Visual Studio C++ desktop toolchain was found. No installation is performed.' }
$taskVcvars = Join-Path $taskVs 'VC\Auxiliary\Build\vcvars64.bat'
$taskEnvLines = & $env:ComSpec /d /s /c ('call "' + $taskVcvars + '" >nul && set')
if ($LASTEXITCODE -ne 0) { throw 'Visual Studio environment setup failed.' }
$taskSavedEnvironment = @{}
foreach ($line in $taskEnvLines) {
    $parts = $line -split '=', 2
    if ($parts.Count -eq 2 -and $parts[0] -and !$parts[0].StartsWith('=')) {
        $taskSavedEnvironment[$parts[0]] = [Environment]::GetEnvironmentVariable($parts[0], 'Process')
        [Environment]::SetEnvironmentVariable($parts[0], $parts[1], 'Process')
    }
}
New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
$taskBuildPath = (Resolve-Path -LiteralPath $BuildDirectory).Path
$taskOutput = Join-Path (Split-Path -Parent $PSScriptRoot) 'CopyConfirm.exe'
Push-Location -LiteralPath $PSScriptRoot
try {
    & rc.exe '/nologo' '/c65001' '/fo' (Join-Path $taskBuildPath 'CopyConfirm.res') 'CopyConfirm.rc'
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
    & cl.exe '/nologo' '/std:c++20' '/O2' '/W4' '/WX' '/EHsc' '/MT' '/utf-8' `
        'CopyConfirm.cpp' (Join-Path $taskBuildPath 'CopyConfirm.res') `
        ('/Fo' + (Join-Path $taskBuildPath 'CopyConfirm.obj')) ('/Fe' + $taskOutput) `
        '/link' '/SUBSYSTEM:WINDOWS' '/INCREMENTAL:NO' 'user32.lib' 'gdi32.lib' 'shell32.lib' `
        'gdiplus.lib' 'ole32.lib' 'runtimeobject.lib' 'uuid.lib' 'advapi32.lib' 'comctl32.lib'
    if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
    Get-Item -LiteralPath $taskOutput | Select-Object FullName,Length
} finally {
    Pop-Location
    foreach ($name in $taskSavedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $taskSavedEnvironment[$name], 'Process')
    }
}
