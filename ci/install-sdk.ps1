param(
    [string]$directory='git-sdk',
    [string]$url='https://github.com/git-for-windows/git-sdk-64/releases/download/ci-artifacts/git-sdk-x86_64-minimal.zip'
)

Invoke-WebRequest "$url" -OutFile git-sdk.zip
Expand-Archive -LiteralPath git-sdk.zip -DestinationPath "$directory"
Remove-Item -Path git-sdk.zip

New-Item -Path .git/info -ItemType Directory -Force
New-Item -Path .git/info/exclude -ItemType File -Force
Add-Content -Path .git/info/exclude -Value "/$directory"
