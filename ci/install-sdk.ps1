param(
    [string]$Directory='git-sdk',
    [string]$URL='https://github.com/git-for-windows/git-sdk-64/releases/download/ci-artifacts/git-sdk-x86_64-minimal.zip',
    [string]$DownloadDirectory='.dependencies'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

New-Item -Path $DownloadDirectory -ItemType Directory -Force

$archive = Join-Path $DownloadDirectory 'git-sdk.zip'
if (-not (Test-Path $archive)) {
    Invoke-WebRequest "$URL" -OutFile $archive -TimeoutSec 300
}

New-Item -Path $Directory -ItemType Directory -Force
tar -xf $archive -C $Directory
if ($LASTEXITCODE -ne 0) {
    throw "Failed to extract $archive with exit code $LASTEXITCODE"
}

New-Item -Path .git/info -ItemType Directory -Force
New-Item -Path .git/info/exclude -ItemType File -Force
Add-Content -Path .git/info/exclude -Value "/$Directory"
Add-Content -Path .git/info/exclude -Value "/$DownloadDirectory"
