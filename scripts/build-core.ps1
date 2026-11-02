param([string]$DependenciesRoot=$env:OBS_NEXUS_DEPS)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
if (!$DependenciesRoot) { $DependenciesRoot=Join-Path (Split-Path $project -Parent) 'OBS Twitch alerts/OBS Twitch Overlays/.deps' }
$prefix=@($DependenciesRoot,(Join-Path $DependenciesRoot 'obs-deps-2025-07-11-x64'),(Join-Path $DependenciesRoot 'obs-deps-qt6-2025-07-11-x64')) -join ';'
& "$PSScriptRoot/Invoke-CMake.ps1" -S "$project/core" -B "$project/build/core" "-DCMAKE_PREFIX_PATH=$prefix" "-Dlibobs_DIR=$DependenciesRoot/cmake"
if ($LASTEXITCODE -ne 0) { throw 'Error al configurar Core.' }
& "$PSScriptRoot/Invoke-CMake.ps1" --build "$project/build/core" --config RelWithDebInfo --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Error al compilar Core.' }
& "$PSScriptRoot/Invoke-CMake.ps1" --build "$project/build/core" --config RelWithDebInfo --target RUN_TESTS
if ($LASTEXITCODE -ne 0) { throw 'Fallaron las pruebas de Core.' }
