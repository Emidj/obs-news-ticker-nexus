param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $CMakeArguments
)
$ErrorActionPreference = 'Stop'
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path $vswhere)) { throw 'Install Visual Studio 2022 C++ Build Tools and CMake first.' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
    Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
$start = [System.Diagnostics.ProcessStartInfo]::new()
$start.FileName = $cmakePath
$start.WorkingDirectory = Split-Path $PSScriptRoot
$start.UseShellExecute = $false
foreach ($argument in $CMakeArguments) { $start.ArgumentList.Add($argument) }
# Some hosts supply both PATH and Path. MSBuild rejects that environment.
# Normalize only the child environment; do not change machine/user settings.
$pathKeys = @($start.Environment.Keys | Where-Object { $_ -ieq 'Path' })
$pathValues = @($pathKeys | ForEach-Object { $start.Environment[$_] })
foreach ($key in $pathKeys) { $start.Environment.Remove($key) | Out-Null }
$start.Environment['Path'] = ($pathValues -join ';')
$process = [System.Diagnostics.Process]::Start($start)
$process.WaitForExit()
exit $process.ExitCode
