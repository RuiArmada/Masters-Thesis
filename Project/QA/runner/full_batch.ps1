param([string[]] $Phases = @('V','A','F','B','D','C','Q','R','J','T','G','E','S'))
# Full re-run in Accurate mode. Behavioural runs: headless, fixed step of 1 sim second per frame.
# Scaling runs (E, S): rendered, real time (30 sim s per real s at whatever frame rate the machine reaches).
. "$PSScriptRoot\lib.ps1"
$c = "$R\cfg"; $out = "$R\full_batch.out"
while (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { Start-Sleep 5 }
$mall = '/Game/UseCases/UrbanPlanning/MallA'
function Done($label) { Test-Path "$A\Run_$label" }   # resume: skip runs already recorded

if ($Phases -contains 'V') {
  foreach ($rep in 1..3) { foreach ($m in 'V01_Unidirectional','V02_Bidirectional','V04_Bottleneck') {
    foreach ($b in 'blend','noblend') {
      $label = "V_${m}_${b}_r$rep"; if (Done $label) { continue }
      $x = @('-RecordPositions=1','-RecordSimTime'); if ($b -eq 'noblend') { $x += '-NoBlend' }
      Run-One "/Game/UseCases/Validation/$m" $label "$c\corridor_s$rep.xml" 'AccurateTime_*.csv' 300 $x -Out $out
  } } }
  "PHASE V COMPLETE" | Tee-Object $out -Append
}
if ($Phases -contains 'A') {
  foreach ($seed in 1..5) { foreach ($cap in 1..3) {
    $label = "A_mallA_cap${cap}_s$seed"; if (Done $label) { continue }
    Run-One $mall $label "$c\Desks_cap${cap}_s$seed.xml" 'Infrastructure_MallA_*.csv' 600 @('-RecordSimTime') -Out $out
  } }
  "PHASE A COMPLETE" | Tee-Object $out -Append
}
if ($Phases -contains 'F') {
  foreach ($seed in 1..5) { foreach ($tag in 'FoodCounter','FoodTable') { foreach ($cap in 1..3) {
    $label = "F_${tag}_cap${cap}_s$seed"; if (Done $label) { continue }
    Run-One $mall $label "$c\${tag}_cap${cap}_s$seed.xml" 'Infrastructure_MallA_*.csv' 600 @('-RecordSimTime') -Out $out
  } } }
  "PHASE F COMPLETE" | Tee-Object $out -Append
}
if ($Phases -contains 'B') {
  foreach ($seed in 1..5) { foreach ($m in 'MallA','MallB') {
    $label = "B_${m}_s$seed"; if (Done $label) { continue }
    Run-One "/Game/UseCases/UrbanPlanning/$m" $label "$c\mall_auto_s$seed.xml" "Infrastructure_${m}_*.csv" 600 @('-RecordSimTime') -Out $out
  } }
  "PHASE B COMPLETE" | Tee-Object $out -Append
}
if ($Phases -contains 'D') {
  foreach ($seed in 1..5) {
    $label = "D_dul_acc_s$seed"; if (Done $label) { continue }
    Run-One '/Game/UseCases/DailyUrbanLife' $label "$c\dul_s$seed.xml" 'AccurateTime_*DailyUrbanLife*.csv' 900 @('-RecordSimTime') -Out $out
  }
  "PHASE D COMPLETE" | Tee-Object $out -Append
}
if ($Phases -contains 'C') {
  foreach ($n in 50,100,200) { foreach ($rep in 1..3) {
    $label = "C_evac_${n}_r$rep"; if (Done $label) { continue }
    Run-One '/Game/UseCases/Evacuation' $label "$c\evac_s$rep.xml" 'Evacuation_*.csv' 900 @("-StressAgents=$n",'-RecordSimTime') -Out $out
  } }
  "PHASE C COMPLETE" | Tee-Object $out -Append
}
if ($Phases -contains 'Q') {
  # Evacuation again with a fixed step of 0.25 simulated seconds (FPS=120): step-size check for the 1.5 s exit service.
  foreach ($n in 50,100,200) { foreach ($rep in 1..3) {
    $label = "Q_evac_${n}_r$rep"; if (Done $label) { continue }
    Run-One '/Game/UseCases/Evacuation' $label "$c\evac_s$rep.xml" 'Evacuation_*.csv' 3600 @("-StressAgents=$n",'-RecordSimTime') -Out $out -Fps 120
  } }
  "PHASE Q COMPLETE" | Tee-Object $out -Append
}
if ($Phases -contains 'T') {
  # Step-size check for the shopping center: counters with one and three places, seeds 1..5, at 0.25 s per frame.
  foreach ($seed in 1..5) { foreach ($cap in 1,3) {
    $label = "T_FoodCounter_cap${cap}_s$seed"; if (Done $label) { continue }
    Run-One $mall $label "$c\FoodCounter_cap${cap}_s$seed.xml" 'Infrastructure_MallA_*.csv' 900 @('-RecordSimTime') -Out $out -Fps 120
  } }
  foreach ($seed in 1..2) {
    $label = "T_dul_s$seed"; if (Done $label) { continue }
    Run-One '/Game/UseCases/DailyUrbanLife' $label "$c\dul_s$seed.xml" 'AccurateTime_*DailyUrbanLife*.csv' 1800 @('-RecordSimTime') -Out $out -Fps 120
  }
  "PHASE T COMPLETE" | Tee-Object $out -Append
}
if ($Phases -contains 'R') {
  foreach ($seed in 1..10) { foreach ($tag in 'FoodCounter','FoodTable') { foreach ($cap in 1,3) {
    $label = "R_rush_${tag}_cap${cap}_s$seed"; if (Done $label) { continue }
    Run-One $mall $label "$c\rush_${tag}_cap${cap}_s$seed.xml" 'Infrastructure_MallA_*.csv' 600 @('-RecordSimTime') -Out $out
  } } }
  "PHASE R COMPLETE" | Tee-Object $out -Append
}
if ($Phases -contains 'J') {
  foreach ($seed in 1..5) {
    $label = "J_jam_n50_FoodCounter_cap1_s$seed"; if (Done $label) { continue }
    Run-One $mall $label "$c\jam_n50_s$seed.xml" 'Infrastructure_MallA_*.csv' 900 @('-StressAgents=50','-StressWarmup=1','-StressSeconds=2','-RecordSimTime') -Out $out
  }
  "PHASE J COMPLETE" | Tee-Object $out -Append
}
if ($Phases -contains 'G') {
  foreach ($seed in 6..30) { foreach ($kind in 'Desks','FoodCounter','FoodTable') { foreach ($cap in 1..3) {
    $label = "G_${kind}_cap${cap}_s$seed"; if (Done $label) { continue }
    Run-One $mall $label "$c\${kind}_cap${cap}_s$seed.xml" 'Infrastructure_MallA_*.csv' 600 @('-RecordSimTime') -Out $out
  } } }
  "PHASE G COMPLETE" | Tee-Object $out -Append
}
$stress = '-StressWarmup=30','-StressSeconds=120','-StressQuit','-ExecCmds="r.VSync 0,t.MaxFPS 0"'
if ($Phases -contains 'E') {
  foreach ($n in 50,100,200,400,800,1600) {
    $reps = if ($n -le 400) { 1..2 } else { 1..1 }
    foreach ($rep in $reps) {
      $label = "E_evac_${n}_r$rep"
      if (-not (Done $label)) { Run-One '/Game/UseCases/Evacuation' $label "$c\evac_s1.xml" 'Stress_*.csv' 900 ($stress + "-StressAgents=$n") -Render -RealTime -Out $out }
      $label = "E_dul_${n}_r$rep"
      if (-not (Done $label)) { Run-One '/Game/UseCases/DailyUrbanLife' $label "$c\dul_x$n.xml" 'Stress_*.csv' 900 ($stress + "-StressAgents=$n") -Render -RealTime -Out $out }
  } }
  "PHASE E COMPLETE" | Tee-Object $out -Append
}
if ($Phases -contains 'S') {
  foreach ($n in 50,100,200,400,800) {
    $label = "S_evac_${n}_nomesh_r1"
    if (-not (Done $label)) { Run-One '/Game/UseCases/Evacuation' $label "$c\evac_s1.xml" 'Stress_*.csv' 900 ($stress + '-StressHideMeshes' + "-StressAgents=$n") -Render -RealTime -Out $out }
    $label = "S_dul_${n}_nomesh_r1"
    if (-not (Done $label)) { Run-One '/Game/UseCases/DailyUrbanLife' $label "$c\dul_x$n.xml" 'Stress_*.csv' 900 ($stress + '-StressHideMeshes' + "-StressAgents=$n") -Render -RealTime -Out $out }
  }
  "PHASE S COMPLETE" | Tee-Object $out -Append
}
"FULL BATCH COMPLETE" | Tee-Object $out -Append
