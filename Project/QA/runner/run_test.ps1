param(
    [Parameter(Mandatory)] [string] $Map,          # e.g. /Game/UseCases/UrbanPlanning/MallA
    [Parameter(Mandatory)] [string] $Label,        # used for log file name
    [ValidateSet('Visual','Fast','Accurate')] [string] $Mode = 'Visual',
    [int] $Seed = 0,
    [int] $TimeoutSeconds = 600,
    [string] $ExpectPattern = '*.csv',
    [switch] $NullRhi,
    [string] $ConfigFile = '',
    [string[]] $ExtraArgs = @(),
    [int] $ResX = 960,
    [int] $ResY = 540
)

$ErrorActionPreference = 'Stop'
# Pause between runs while a file named PAUSE exists next to this script.
$pauseFlag = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'PAUSE'
# (no pause check in the test copy)
$project   = 'C:\Users\Rui\Documents\GitHub\BOIDS\Thesis\Thesis.uproject'
$editor    = 'D:\UE\UE_5.4\Engine\Binaries\Win64\UnrealEditor.exe'
$saved     = 'C:\Users\Rui\Documents\GitHub\BOIDS\Thesis\Saved'
$analytics = Join-Path $saved 'Analytics'
$cfgDir    = Join-Path $saved 'Config'
$scratch   = Split-Path -Parent $MyInvocation.MyCommand.Path
$logDir    = Join-Path $scratch 'runlogs'
New-Item -ItemType Directory -Force $analytics, $cfgDir, $logDir | Out-Null

# Run configuration. Only settings we deliberately change are written; everything else stays at code defaults.
$config = @"
<?xml version="1.0" encoding="UTF-8"?>
<simulationConfig>
  <features>
    <feature name="enableEvacuation" value="true"/>
    <feature name="enableInfrastructureAnalysis" value="true"/>
  </features>
  <simulationMode mode="$Mode"/>
  <infrastructurePlanning>
    <runDurationSimMinutes value="160"/>
  </infrastructurePlanning>
  <fastSimulation>
    <runDurationSimMinutes value="200"/>
    <randomSeed value="$Seed"/>
    <autoExportOnEnd value="true"/>
  </fastSimulation>
</simulationConfig>
"@
if ($ConfigFile) { Copy-Item $ConfigFile (Join-Path $cfgDir 'SimulationConfig.xml') -Force } else { Set-Content -Path (Join-Path $cfgDir 'SimulationConfig.xml') -Value $config -Encoding utf8 }

$before = @(Get-ChildItem $analytics -Filter $ExpectPattern -File -ErrorAction SilentlyContinue | ForEach-Object FullName)
$log = Join-Path $logDir "$Label.log"
$argList = @("`"$project`"", $Map, '-game', '-windowed', "-ResX=$ResX", "-ResY=$ResY", '-nosound', '-nosplash',
             '-unattended', '-NoVerifyGC', "-abslog=`"$log`"")
if ($NullRhi) { $argList += '-nullrhi'; $argList += '-RenderOffScreen' }   # no visible window for unrendered runs
$argList += '-WinX=-1080'; $argList += '-WinY=-243'                          # otherwise open on the OMEN (left, portrait) screen
if ($ExtraArgs) { $argList += $ExtraArgs }

$start = Get-Date
$proc = Start-Process -FilePath $editor -ArgumentList $argList -PassThru
$newFiles = @()
while (((Get-Date) - $start).TotalSeconds -lt $TimeoutSeconds -and -not $proc.HasExited) {
    Start-Sleep -Seconds 3
    $newFiles = @(Get-ChildItem $analytics -Filter $ExpectPattern -File -ErrorAction SilentlyContinue |
                  Where-Object { $before -notcontains $_.FullName })
    if ($newFiles.Count -gt 0) { Start-Sleep -Seconds 4; break }   # let any companion files finish writing
}
$elapsed = [int]((Get-Date) - $start).TotalSeconds
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; $proc.WaitForExit() }

$newFiles = @(Get-ChildItem $analytics -File -ErrorAction SilentlyContinue | Where-Object { $before -notcontains $_.FullName -and $_.LastWriteTime -ge $start })
[pscustomobject]@{
    Label   = $Label
    Mode    = $Mode
    Seed    = $Seed
    Seconds = $elapsed
    Files   = ($newFiles | ForEach-Object Name) -join '; '
    Log     = $log
}





