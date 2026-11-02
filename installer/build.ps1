$ErrorActionPreference='Stop'
$project = Split-Path $PSScriptRoot -Parent
$version=(Get-Content (Join-Path $project 'VERSION') -Raw).Trim()
if($version -notmatch '^\d+\.\d+\.\d+$'){throw 'Invalid release version'}
Push-Location $project
try {
    $files = [ordered]@{
        plugin='build/obs-news-ticker-nexus.dll'
        core='build/core/RelWithDebInfo/nexus-twitch-core.dll'
        es='data/locale/es-ES.ini'
        en='data/locale/en-US.ini'
        it='data/locale/it-IT.ini'
        fr='data/locale/fr-FR.ini'
        de='data/locale/de-DE.ini'
        'installer-locale'='installer/strings.json'
        branding='installer/branding/nexus-sidebar.png'
        'app-icon'='installer/branding/nexus.ico'
        effect='data/effects/ticker-fade.effect'
        'map-artwork'='data/graphics/broadcast-reference.png'
        'design-artwork'='data/graphics/broadcast-designs.png'
        'compact-artwork'='data/graphics/compact-reference.png'
        'graphics-guide'='data/graphics/README.md'
        readme='README.md'
        'core-guide'='docs/twitch-core.md'
        'broadcast-guide'='docs/broadcast-reference.md'
        'now-playing-guide'='docs/now-playing-setup.md'
        'weather-guide'='docs/weather.md'
        'hype-guide'='docs/hype-train.md'
        'json-guide'='docs/now-playing-json-v1.md'
        'release-guide'='docs/RELEASE-1.0.0.md'
        'core-provenance'='core/PROVENANCE.md'
        'weather-capitals'='data/weather-capitals.json'
        license='LICENSE'
        'core-contract'='core/shared/core-contract.json'
        'ticker-contract'='shared/ticker-contract.json'
    }
    if ((Get-Item $files.core).VersionInfo.ProductVersion -ne '1.3.1') { throw 'Core no compilado o versiÃ³n incorrecta.' }
    $previous = (Get-Content shared/known-previous-core.json -Raw | ConvertFrom-Json).SHA256
    $previousTicker = (Get-Content shared/known-previous-core.json -Raw | ConvertFrom-Json).Ticker120SHA256
    $previousHype = (Get-Content shared/known-previous-core.json -Raw | ConvertFrom-Json).Hype130SHA256
    $hashes = foreach ($name in $files.Keys) {
        $hash = (Get-FileHash -LiteralPath $files[$name]).Hash.ToLowerInvariant()
        '{"'+$name+'","'+$hash+'"}'
    }
    $metadata = 'using System.Collections.Generic; internal static class Payload { public const string Version="'+$version+'",PreviousCoreHash="'+$previous+'",PreviousTickerCoreHash="'+$previousTicker+'",PreviousHypeCoreHash="'+$previousHype+'"; public static readonly Dictionary<string,string> Hashes=new Dictionary<string,string>{'+($hashes -join ',')+'}; }'
    [IO.File]::WriteAllText((Join-Path $project 'build/InstallerPayload.cs'),$metadata,(New-Object Text.UTF8Encoding($false)))
    New-Item -ItemType Directory -Force dist | Out-Null
    $destination = Join-Path $project ("dist/OBS-News-Ticker-NEXUS-"+$version+"-Windows-x64-Setup.exe")
    $arguments = @('/nologo','/target:winexe','/platform:x64','/optimize+','/win32icon:installer/branding/nexus.ico','/win32manifest:installer/app.manifest',"/out:$destination",'/reference:System.Windows.Forms.dll','/reference:System.Drawing.dll','/reference:System.Web.Extensions.dll','/reference:System.Core.dll')
    foreach ($name in $files.Keys) { $arguments += '/resource:'+($files[$name])+','+$name }
    $arguments += @((Join-Path $project 'installer\Setup.cs'),(Join-Path $project 'installer\I18n.cs'),(Join-Path $project 'build\InstallerPayload.cs'))
    & 'C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe' @arguments
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo compilar el instalador.' }
    $testManifest = (Get-Content installer/app.manifest -Raw).Replace('requireAdministrator','asInvoker')
    [IO.File]::WriteAllText((Join-Path $project 'build/installer-test.manifest'),$testManifest)
    $testArguments = $arguments | ForEach-Object {
        if ($_ -eq "/out:$destination") { '/out:'+(Join-Path $project 'build/installer-test.exe') }
        elseif ($_ -eq '/win32manifest:installer/app.manifest') { '/win32manifest:build/installer-test.manifest' }
        else { $_ }
    }
    & 'C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe' @testArguments
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo compilar la autoprueba del instalador.' }
    $coreDirectory = 'dist/obs-news-ticker-nexus/bin/64bit'
    Copy-Item -LiteralPath $files.core -Destination "$coreDirectory/nexus-twitch-core.dll" -Force
    Get-FileHash -LiteralPath $destination | ForEach-Object { $_.Hash.ToLowerInvariant()+'  '+(Split-Path $_.Path -Leaf) } | Set-Content dist/SHA256SUMS.txt -Encoding ascii
    Write-Output $destination
} finally { Pop-Location }
