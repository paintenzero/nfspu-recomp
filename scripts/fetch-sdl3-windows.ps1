# Pinned official MinGW development packages. Nothing is installed globally.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskSdlDir = Join-Path $taskRoot 'third_party/sdl'
New-Item -ItemType Directory -Force -Path $taskSdlDir | Out-Null
$taskPackages = @(
    @{
        File = 'SDL3-devel-3.4.18-mingw.zip'
        Url = 'https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-mingw.zip'
        Sha256 = 'CD98158C8D025A816F430A600B4E9526524E6219EE884EC72BD498B80FD3BF58'
    },
    @{
        File = 'SDL3_ttf-devel-3.2.2-mingw.zip'
        Url = 'https://github.com/libsdl-org/SDL_ttf/releases/download/release-3.2.2/SDL3_ttf-devel-3.2.2-mingw.zip'
        Sha256 = 'E44E78D6DEC080F1D96BD8AC1BAC98DDC2574EFF980D777C223C799C5A03B1BA'
    }
)
foreach ($taskPackage in $taskPackages) {
    $taskZip = Join-Path $taskSdlDir $taskPackage.File
    if (-not (Test-Path -LiteralPath $taskZip)) {
        Write-Host "Downloading $($taskPackage.File)"
        Invoke-WebRequest -UseBasicParsing -Uri $taskPackage.Url -OutFile $taskZip
    }
    $taskActual = (Get-FileHash -LiteralPath $taskZip -Algorithm SHA256).Hash
    if ($taskActual -cne $taskPackage.Sha256) {
        throw "SHA256 mismatch for $taskZip. Expected $($taskPackage.Sha256), found $taskActual"
    }
    Expand-Archive -LiteralPath $taskZip -DestinationPath $taskSdlDir -Force
    Write-Host "Verified and extracted $($taskPackage.File)"
}
