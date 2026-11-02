param([string]$ObsRoot = 'C:\Program Files\obs-studio')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$oldPath = $env:PATH
Push-Location $projectRoot
try {
    $env:PATH = "$ObsRoot\bin\64bit;$oldPath"
    & "$projectRoot\build\weather-test.exe" | Tee-Object -FilePath "$projectRoot\validation\weather-test.log"
    if ($LASTEXITCODE -ne 0) { throw "Weather test failed: $LASTEXITCODE" }
    & "$projectRoot\build\now-playing-obs-test.exe" | Tee-Object -FilePath "$projectRoot\validation\now-playing-obs-test.log"
    if ($LASTEXITCODE -ne 0) { throw "Now Playing bridge test failed: $LASTEXITCODE" }
    & "$projectRoot\build\twitch-observer-test.exe" | Tee-Object -FilePath "$projectRoot\validation\twitch-observer-test.log"
    if ($LASTEXITCODE -ne 0) { throw "Twitch observer test failed: $LASTEXITCODE" }
    $env:PATH = "$ObsRoot\bin\64bit;$oldPath"
    & "$projectRoot\build\integration-test.exe" $ObsRoot "$projectRoot\dist\obs-news-ticker-nexus\bin\64bit\obs-news-ticker-nexus.dll" "$projectRoot\dist\obs-news-ticker-nexus\data" 2>&1 | Tee-Object -FilePath "$projectRoot\validation\phase1-test.log"
    if ($LASTEXITCODE -ne 0) { throw "Integration test failed: $LASTEXITCODE" }
} finally {
    $env:PATH = $oldPath
    Pop-Location
}
