$R = Split-Path -Parent $MyInvocation.MyCommand.Path
$A = 'C:\Users\Rui\Documents\GitHub\BOIDS\Thesis\Saved\Analytics'
$nav = '-ini:Engine:[/Script/NavigationSystem.RecastNavMesh]:RuntimeGeneration=Dynamic'
$fixedStep = @('-UseFixedTimeStep', '-FPS=30')   # 1 frame = 1 sim second (dilation 30)

# Turn an old run configuration into an Accurate one: mode, seed and run length.
function Make-Accurate([string]$Src, [string]$Dst, [int]$Seed, [int]$RunMinutes) {
  $x = Get-Content $Src -Raw
  $x = [regex]::Replace($x, '\s*<simulationMode[^>]*/>', '')
  $x = [regex]::Replace($x, '(?s)\s*<fastSimulation>.*?</fastSimulation>', '')
  $block = "`n  <simulationMode mode=`"Accurate`"/>`n  <fastSimulation>`n    <runDurationSimMinutes value=`"$RunMinutes`"/>`n    <randomSeed value=`"$Seed`"/>`n    <autoExportOnEnd value=`"true`"/>`n  </fastSimulation>`n</simulation>"
  $x = $x -replace '</simulation>\s*$', $block
  Set-Content $Dst $x -Encoding utf8
}

function Run-One($Map, $Label, $Cfg, $Expect, $Timeout, [string[]]$Extra, [switch]$Render, [switch]$RealTime, $Out, [int]$Fps = 30) {
  $args2 = @($nav, "-RunRecord=$Label") + $Extra
  if (-not $RealTime) { $args2 += @('-UseFixedTimeStep', "-FPS=$Fps") }
  if ($Render) { $res = & "$R\run_scenario.ps1" -Map $Map -Label $Label -Mode Accurate -ConfigFile $Cfg -TimeoutSeconds $Timeout -ExpectPattern $Expect -ResX 1920 -ResY 1080 -ExtraArgs $args2 }
  else { $res = & "$R\run_scenario.ps1" -Map $Map -Label $Label -Mode Accurate -ConfigFile $Cfg -TimeoutSeconds $Timeout -ExpectPattern $Expect -NullRhi -ExtraArgs $args2 }
  $crash = Select-String $res.Log -Pattern 'Assertion failed|Fatal error' -Quiet
  $tripsPath = "$A\Run_$Label\trips.csv"
  $fails = 0; $starts = 0
  if (Test-Path $tripsPath) { $t = Import-Csv $tripsPath; $fails = @($t | ? event -eq fail).Count; $starts = @($t | ? event -eq start).Count }
  $line = "DONE $Label secs=$($res.Seconds) trips=$starts fails=$fails crash=$crash files=$($res.Files)"
  if ($Out) { $line | Tee-Object $Out -Append } else { $line }
}
