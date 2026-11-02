param(
    [ValidateRange(90,1800)][int]$Seconds=180,
    [switch]$HypePreview,
    [switch]$SmokeOnly,
    [string]$PlayingDll=(Join-Path (Split-Path $PSScriptRoot -Parent) '../OBS Playing/work/core-phase08h-build/RelWithDebInfo/obs-playing-next.dll')
)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$qa=Join-Path $project 'build/obs-qa'
$exe=Join-Path $qa 'bin/64bit/obs64.exe'
if(!(Test-Path -LiteralPath $exe)){throw 'The isolated portable OBS fixture must exist in build/obs-qa.'}
if(!(Test-Path -LiteralPath $PlayingDll)){throw 'Provide the current native Playing development DLL.'}
$qaPath=[IO.Path]::GetFullPath($qa)
foreach($running in @(Get-Process obs64 -ErrorAction SilentlyContinue)){
    if($running.Path -and $running.Path.StartsWith($qaPath,[StringComparison]::OrdinalIgnoreCase)){throw 'Close the portable QA OBS before running this test.'}
}
$probe=Join-Path $qa 'obs-plugins/64bit/nexus-playing-bridge-qa.dll'
if(Test-Path -LiteralPath $probe){throw 'A test module already exists in the portable OBS; inspect it before replacing it.'}
$log=Join-Path $project 'validation/portable-soak.log'
$configBackups=@{}
$process=$null
$envNames=@('NEXUS_TICKER_BRIDGE_SELFTEST','NEXUS_TICKER_BRIDGE_LOG','NEXUS_TICKER_SOAK_SECONDS','NEXUS_TICKER_HYPE_PREVIEW_QA')
$priorEnvironment=@{}
try {
    Copy-Item -LiteralPath (Join-Path $project 'build/obs-news-ticker-nexus.dll') -Destination (Join-Path $qa 'obs-plugins/64bit/obs-news-ticker-nexus.dll') -Force
    Copy-Item -LiteralPath (Join-Path $project 'build/core/RelWithDebInfo/nexus-twitch-core.dll') -Destination (Join-Path $qa 'bin/64bit/nexus-twitch-core.dll') -Force
    Copy-Item -LiteralPath $PlayingDll -Destination (Join-Path $qa 'obs-plugins/64bit/obs-playing-next.dll') -Force
    # Copy contents explicitly to keep the same directory layout on repeat runs.
    Copy-Item -Path (Join-Path $project 'data/*') -Destination (Join-Path $qa 'data/obs-plugins/obs-news-ticker-nexus') -Recurse -Force
    New-Item -ItemType Directory -Force (Join-Path $qa 'data/obs-plugins/nexus-playing-bridge-qa') | Out-Null
    New-Item -ItemType Directory -Force (Join-Path $qa 'data/obs-plugins/obs-playing-next') | Out-Null
    Copy-Item -LiteralPath (Join-Path $project 'build/playing-real-obs-probe.dll') -Destination $probe
    foreach($name in @('global.ini','user.ini')){
        $file=Join-Path $qa "config/obs-studio/$name"
        if(Test-Path -LiteralPath $file){
            $configBackups[$file]=[IO.File]::ReadAllBytes($file)
            $text=[IO.File]::ReadAllText($file)
            if($text -match '(?m)^ConfirmOnExit='){$text=$text -replace '(?m)^ConfirmOnExit=.*','ConfirmOnExit=false'}
            else {$text=$text.Replace('[General]',"[General]`r`nConfirmOnExit=false")}
            [IO.File]::WriteAllText($file,$text,[Text.UTF8Encoding]::new($false))
        }
    }
    if(Test-Path -LiteralPath $log){Copy-Item -LiteralPath $log -Destination "$log.previous" -Force}
    foreach($name in $envNames){$priorEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
    [Environment]::SetEnvironmentVariable('NEXUS_TICKER_BRIDGE_SELFTEST','1','Process')
    [Environment]::SetEnvironmentVariable('NEXUS_TICKER_BRIDGE_LOG',$log,'Process')
    [Environment]::SetEnvironmentVariable('NEXUS_TICKER_SOAK_SECONDS',$(if($SmokeOnly){'0'}else{"$Seconds"}),'Process')
    [Environment]::SetEnvironmentVariable('NEXUS_TICKER_HYPE_PREVIEW_QA',$(if($HypePreview){'1'}else{'0'}),'Process')
    $process=Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -ArgumentList '--portable --multi --disable-updater --disable-shutdown-check' -WindowStyle Hidden -PassThru
    foreach($name in $envNames){[Environment]::SetEnvironmentVariable($name,$priorEnvironment[$name],'Process')}
    $deadline=[DateTime]::UtcNow.AddSeconds($Seconds+120)
    while(!$process.WaitForExit(1000)){
        if([DateTime]::UtcNow -gt $deadline){$null=$process.CloseMainWindow();$null=$process.WaitForExit(10000);throw 'Portable OBS exceeded the bounded test duration.'}
    }
    if($process.ExitCode -ne 0){throw "Portable OBS exited with code $($process.ExitCode)."}
    if(!(Test-Path -LiteralPath $log)){throw 'Portable probe produced no evidence.'}
    $result=Get-Content -LiteralPath $log -Tail 1
    if($result -notmatch '^RESULT checks=\d+ failures=0$'){throw "Portable probe failed: $result"}
    Write-Output $result
    $latest=Get-ChildItem (Join-Path $qa 'config/obs-studio/logs/*.txt') | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if($latest){Copy-Item -LiteralPath $latest.FullName -Destination (Join-Path $project 'validation/portable-soak-obs.log') -Force}
    [ordered]@{
        Seconds=$(if($SmokeOnly){0}else{$Seconds});ExitCode=$process.ExitCode;Result=$result
        TickerSHA256=(Get-FileHash -LiteralPath (Join-Path $qa 'obs-plugins/64bit/obs-news-ticker-nexus.dll')).Hash.ToLowerInvariant()
        PlayingSHA256=(Get-FileHash -LiteralPath (Join-Path $qa 'obs-plugins/64bit/obs-playing-next.dll')).Hash.ToLowerInvariant()
        Boundary='Real portable OBS; demo songs; real Open-Meteo weather; no real DJ, Twitch authorization or streaming.'
    } | ConvertTo-Json | Set-Content (Join-Path $project 'validation/portable-soak.json')
} finally {
    foreach($name in $priorEnvironment.Keys){[Environment]::SetEnvironmentVariable($name,$priorEnvironment[$name],'Process')}
    if(!$process -or $process.HasExited){
        if(Test-Path -LiteralPath $probe){Remove-Item -LiteralPath $probe}
        foreach($file in $configBackups.Keys){[IO.File]::WriteAllBytes($file,$configBackups[$file])}
    }
}
