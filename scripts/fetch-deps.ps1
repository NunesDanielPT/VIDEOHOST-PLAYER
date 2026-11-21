# Descarrega as dependências binárias para third_party/ (Windows, PowerShell).
#   powershell -ExecutionPolicy Bypass -File scripts/fetch-deps.ps1
#
# FFmpeg: build LGPL partilhada (DLLs) da BtbN - compatível com software comercial
# desde que as DLLs sejam distribuídas separadas e sem modificações.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$third = Join-Path $root "third_party"
New-Item -ItemType Directory -Force -Path $third | Out-Null

$ffmpegDir = Join-Path $third "ffmpeg"
if (Test-Path (Join-Path $ffmpegDir "include/libavformat/avformat.h")) {
    Write-Host "FFmpeg já existe em $ffmpegDir"
    exit 0
}

# Escolhe a versão estável mais recente (ex. ffmpeg-n7.1-latest-win64-lgpl-shared-7.1.zip),
# nunca a "master". O nome dos ficheiros muda com as versões, por isso lê a lista pela API.
$headers = @{ "User-Agent" = "vthost-build" }
if ($env:GITHUB_TOKEN) { $headers["Authorization"] = "Bearer $env:GITHUB_TOKEN" }
$release = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/repos/BtbN/FFmpeg-Builds/releases/tags/latest"
$asset = $release.assets |
    Where-Object { $_.name -match '^ffmpeg-n(\d+)\.(\d+)-latest-win64-lgpl-shared-[\d.]+\.zip$' } |
    Sort-Object { [int]($_.name -replace '^ffmpeg-n(\d+)\.(\d+).*', '$1') * 100 + [int]($_.name -replace '^ffmpeg-n(\d+)\.(\d+).*', '$2') } -Descending |
    Select-Object -First 1
if (-not $asset) { throw "Não encontrei um build FFmpeg win64-lgpl-shared na release 'latest' da BtbN" }

$name = [System.IO.Path]::GetFileNameWithoutExtension($asset.name)
$url = $asset.browser_download_url
$zip = Join-Path $third $asset.name

Write-Host "A descarregar $url"
Invoke-WebRequest -Uri $url -OutFile $zip
Expand-Archive -Path $zip -DestinationPath $third -Force
if (Test-Path $ffmpegDir) { Remove-Item -Recurse -Force $ffmpegDir }
Move-Item (Join-Path $third $name) $ffmpegDir
Remove-Item $zip
Write-Host "FFmpeg pronto em $ffmpegDir"
