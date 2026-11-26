[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$QtDir,
    [string]$BuildDir = (Join-Path $PSScriptRoot 'build'),
    [string]$Destination = (Join-Path $PSScriptRoot 'portable'),
    [string]$CrtDir
)
$ErrorActionPreference = 'Stop'
$qtRoot = (Resolve-Path -LiteralPath $QtDir).Path
$release = Join-Path $BuildDir 'Release/Afterimage.exe'
if (-not (Test-Path -LiteralPath $release -PathType Leaf)) {
    $release = Join-Path $BuildDir 'Afterimage.exe'
}
if (-not (Test-Path -LiteralPath $release -PathType Leaf)) {
    throw 'Build the Release target before packaging.'
}
$dest = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -LiteralPath $release -Destination (Join-Path $dest 'Afterimage.exe') -Force
$env:PATH = "$qtRoot\bin;$env:PATH"
$deployArgs = @('--release','--no-compiler-runtime','--no-opengl-sw','--no-system-d3d-compiler',
    '--no-system-dxc-compiler','--no-translations','--skip-plugin-types','generic,iconengines,styles',
    '--exclude-plugins','qicns,qico,qsvg,qtga,qwbmp,qopensslbackend,qcertonlybackend',
    '--dir',$dest,(Join-Path $dest 'Afterimage.exe'))
& (Join-Path $qtRoot 'bin/windeployqt.exe') @deployArgs
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
if (-not $CrtDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $redistRoot = Join-Path $install 'VC/Redist/MSVC'
    $version = Get-ChildItem -LiteralPath $redistRoot -Directory | Where-Object { $_.Name -match '^\d+(\.\d+)+$' } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    $CrtDir = Join-Path $version.FullName 'x64/Microsoft.VC143.CRT'
}
Get-ChildItem -LiteralPath $CrtDir -File -Filter '*.dll' | Copy-Item -Destination $dest -Force
$lang = Join-Path $dest 'translations'
New-Item -ItemType Directory -Path $lang -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $qtRoot 'translations/qtbase_zh_CN.qm') -Destination $lang -Force
@('[Paths]','Prefix=.','Plugins=.','Translations=translations') | Set-Content -Encoding Ascii -LiteralPath (Join-Path $dest 'qt.conf')
$noticeDir = Join-Path $dest 'licenses'
New-Item -ItemType Directory -Path $noticeDir -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'licenses') -File | Copy-Item -Destination $noticeDir -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'QUICKSTART.txt') -Destination $dest -Force
Write-Output "Portable application: $(Join-Path $dest 'Afterimage.exe')"

