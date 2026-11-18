param([string]$Zig = 'zig', [string]$Plugin)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $out = (New-Item -ItemType Directory -Force -Path 'dist').FullName
    if (!$Plugin) {
        & $Zig cc -std=c23 -O2 -Isrc -shared src/slr.c src/assist.c src/camera_math.c -o dist/FH2DriftAssist.dll
        if ($LASTEXITCODE) { throw 'Mod compilation failed.' }
        $Plugin = 'dist/FH2DriftAssist.dll'
    }
    $binary = (Resolve-Path -LiteralPath $Plugin).Path
    $compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    & $compiler /nologo /optimize+ /platform:x64 /target:exe /codepage:65001 /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll /r:System.Windows.Forms.dll "/out:$out\Install.exe" "/resource:$binary,plugin" /resource:preset\FH2DriftAssist.ini,preset /resource:installer\Garage.xml,garage installer\Install.cs installer\GarageTable.cs
    if ($LASTEXITCODE) { throw 'Installer compilation failed.' }
    Write-Host "Built $out\Install.exe"
} finally { Pop-Location }
