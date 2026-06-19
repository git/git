param(
    [string]$DownloadDirectory = '.dependencies'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$GitVersion = '2.54.0.windows.1'
$MesonVersion = '1.11.0'
$RustVersion = '1.96.0'

New-Item -Path $DownloadDirectory -ItemType Directory -Force | Out-Null
New-Item -Path .git/info -ItemType Directory -Force | Out-Null
New-Item -Path .git/info/exclude -ItemType File -Force | Out-Null
Add-Content -Path .git/info/exclude -Value "/$DownloadDirectory"

function Get-Installer {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Url
    )

    $path = Join-Path $DownloadDirectory $Name
    if (-not (Test-Path $path)) {
        Write-Host "Downloading $Url"
        Invoke-WebRequest $Url -OutFile $path -TimeoutSec 300
    }
    return $path
}

function Invoke-Installer {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList
    )

    Write-Host "Running $FilePath $($ArgumentList -join ' ')"
    $process = Start-Process -Wait -PassThru -FilePath $FilePath -ArgumentList $ArgumentList
    if ($process.ExitCode -ne 0) {
        throw "$FilePath failed with exit code $($process.ExitCode)"
    }
}

$gitAssetVersion = $GitVersion -replace '\.windows\.\d+$', ''
$gitInstaller = Get-Installer "Git-Installer.exe" `
    "https://github.com/git-for-windows/git/releases/download/v$GitVersion/PortableGit-$gitAssetVersion-64-bit.7z.exe"
Invoke-Installer $gitInstaller @('-y', '-o"C:\Program Files\Git"')

$mesonMsi = Get-Installer "meson.msi" `
    "https://github.com/mesonbuild/meson/releases/download/$MesonVersion/meson-$MesonVersion-64.msi"
Invoke-Installer msiexec.exe @('/i', $mesonMsi, 'INSTALLDIR=C:\Meson', '/quiet', '/norestart')

$rustMsi = Get-Installer "rust.msi" `
    "https://static.rust-lang.org/dist/rust-$RustVersion-x86_64-pc-windows-msvc.msi"
Invoke-Installer msiexec.exe @('/i', $rustMsi, 'INSTALLDIR=C:\Rust', 'ADDLOCAL=Rustc,Cargo,Std', '/quiet', '/norestart')
