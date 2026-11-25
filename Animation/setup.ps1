[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('FH2','FM6')][string]$Game,
    [Parameter(Mandatory=$true)][string]$Azimuth,
    [Parameter(Mandatory=$true)][string]$Mount,
    [string]$Python, [string]$FH6Media, [string]$AclDll,
    [string]$GrannyDll, [string]$XcompressDll, [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'

function Select-LocalFile([string]$Title, [string]$Filename, [string]$Start) {
    Add-Type -AssemblyName System.Windows.Forms
    $picker = New-Object System.Windows.Forms.OpenFileDialog
    try {
        $picker.Title = $Title
        $picker.Filter = "$Filename|$Filename"
        if ($Start -and (Test-Path -LiteralPath $Start -PathType Container)) { $picker.InitialDirectory = $Start }
        if ($picker.ShowDialog() -ne 'OK') { throw "Selection cancelled: $Title" }
        return $picker.FileName
    } finally { $picker.Dispose() }
}

function Find-Python([string]$Preferred) {
    $candidates = @($Preferred, $env:DRIFT_ANIMATION_PYTHON)
    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($py -and !$Preferred) {
        try { $candidates += (& $py.Source -3.12 -c 'import sys; print(sys.executable)' 2>$null) } catch {}
    }
    $cmd = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source -notmatch '\\Microsoft\\WindowsApps\\') { $candidates += $cmd.Source }
    $candidates += @("$env:LocalAppData\Programs\Python\Python312\python.exe",
                     "$env:LocalAppData\Python\pythoncore-3.12-64\python.exe")
    foreach ($p in $candidates) {
        if ($p -and (Test-Path -LiteralPath $p -PathType Leaf)) {
            try {
                & $p -B -c 'import numpy,struct,sys; sys.exit(0 if sys.version_info >= (3,10) and struct.calcsize(chr(80))==8 else 1)' 2>$null
                if ($LASTEXITCODE -eq 0) { return $p }
            } catch {}
        }
    }
    Write-Host 'Select a 64-bit Python installation with NumPy. Tested with Python 3.12. To install NumPy: py -3.12 -m pip install numpy'
    $p = Select-LocalFile 'Select python.exe (64-bit Python with NumPy)' 'python.exe' ''
    & $p -B -c 'import numpy,struct,sys; sys.exit(0 if sys.version_info >= (3,10) and struct.calcsize(chr(80))==8 else 1)'
    if ($LASTEXITCODE -ne 0) { throw 'The selected Python needs 64-bit Python 3.10 or later and NumPy.' }
    return $p
}

try {
    $cfgPath = Join-Path $Azimuth "DriftAssistAnimation\$Game\config.json"
    $cfg = $null
    if (Test-Path -LiteralPath $cfgPath) {
        $cfg = Get-Content -LiteralPath $cfgPath -Encoding UTF8 -Raw | ConvertFrom-Json
    }
    if ($Uninstall -and !$cfg) { Write-Host 'No animation installation is registered.'; exit 0 }
    if (!$Python -and $cfg) { $Python = $cfg.python }
    $runtime = Find-Python $Python
    if ($Uninstall) {
        & $runtime -B $cfg.manager uninstall --config $cfgPath
        exit $LASTEXITCODE
    }
    if (!$FH6Media -and $cfg -and (Test-Path -LiteralPath $cfg.fh6)) { $FH6Media = $cfg.fh6 }
    if (!$AclDll -and $cfg -and (Test-Path -LiteralPath $cfg.acl)) { $AclDll = $cfg.acl }
    if (!$GrannyDll -and $cfg -and (Test-Path -LiteralPath $cfg.granny)) { $GrannyDll = $cfg.granny }
    if (!$XcompressDll -and $cfg -and (Test-Path -LiteralPath $cfg.xcompress)) { $XcompressDll = $cfg.xcompress }
    if (!$FH6Media) {
        $fh6Archive = Select-LocalFile 'Select animation.zip in your FH6 installation (Content/media)' 'animation.zip' ''
        $FH6Media = Split-Path -Parent $fh6Archive
    }
    if (!$AclDll) { $AclDll = Select-LocalFile 'Select forzatech_acl.dll from Forza Mod Tool' 'forzatech_acl.dll' '' }
    if (!$GrannyDll) { $GrannyDll = Select-LocalFile 'Select granny2_x64.dll from ForzaTechStudio Native' 'granny2_x64.dll' '' }
    if (!$XcompressDll) {
        $besideGranny = Join-Path (Split-Path -Parent $GrannyDll) 'xcompress64.dll'
        if (Test-Path -LiteralPath $besideGranny -PathType Leaf) { $XcompressDll = $besideGranny }
        else { $XcompressDll = Select-LocalFile 'Select xcompress64.dll from ForzaTechStudio Native' 'xcompress64.dll' '' }
    }
    & $runtime -B (Join-Path $PSScriptRoot 'manager.py') setup --game $Game --azimuth $Azimuth --mount $Mount --fh6 $FH6Media --acl $AclDll --granny $GrannyDll --xcompress $XcompressDll
    exit $LASTEXITCODE
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
