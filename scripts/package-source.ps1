$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$version=(Get-Content (Join-Path $project 'VERSION') -Raw).Trim()
$stage=Join-Path $project ('build/source-'+[guid]::NewGuid().ToString('N'))
$root=Join-Path $stage ("OBS-News-Ticker-NEXUS-"+$version)
New-Item -ItemType Directory -Force $root | Out-Null
foreach ($entry in @('src','shared','core','data','docs','scripts','tests','installer','examples','integrations','README.md','VERSION','LICENSE','.gitignore')) {
    Copy-Item -LiteralPath (Join-Path $project $entry) -Destination $root -Recurse -Force
}
$destination=Join-Path $project ("dist/OBS-News-Ticker-NEXUS-"+$version+"-Source.zip")
Compress-Archive -LiteralPath $root -DestinationPath $destination -Force
$installer=Join-Path $project ("dist/OBS-News-Ticker-NEXUS-"+$version+"-Windows-x64-Setup.exe")
Get-FileHash -LiteralPath $installer,$destination | ForEach-Object { $_.Hash.ToLowerInvariant()+'  '+(Split-Path $_.Path -Leaf) } | Set-Content (Join-Path $project 'dist/SHA256SUMS.txt') -Encoding ascii
Write-Output $destination
