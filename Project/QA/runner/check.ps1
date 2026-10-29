. "$PSScriptRoot\lib.ps1"
$out = "$R\check2.out"; $c = "$R\cfg"
& "D:\UE\UE_5.4\Engine\Build\BatchFiles\Build.bat" ThesisEditor Win64 Development "-Project=C:\Users\Rui\Documents\GitHub\BOIDS\Thesis\Thesis.uproject" -WaitMutex -NoHotReloadFromIDE '-CompilerVersion=14.38.33130' *> "$R\build.log"
"build exit $LASTEXITCODE" | Tee-Object $out -Append
if ($LASTEXITCODE -ne 0) { return }
Run-One '/Game/UseCases/UrbanPlanning/MallA' 'Z_chk2_mallA_food_s3' "$c\FoodCounter_cap1_s3.xml" 'Infrastructure_MallA_*.csv' 600 @('-RecordSimTime') -Out $out
Run-One '/Game/UseCases/UrbanPlanning/MallB' 'Z_chk2_mallB_s1' "$c\mall_auto_s1.xml" 'Infrastructure_MallB_*.csv' 600 @('-RecordSimTime') -Out $out
Run-One '/Game/UseCases/DailyUrbanLife' 'Z_chk2_dul_s1' "$c\dul_s1.xml" 'FastSim_*DailyUrbanLife*.csv' 900 @('-RecordSimTime') -Out $out
Run-One '/Game/UseCases/Evacuation' 'Z_chk2_evac_100' "$c\evac_s1.xml" 'Evacuation_*.csv' 900 @('-StressAgents=100','-RecordSimTime') -Out $out
Run-One '/Game/UseCases/Validation/V02_Bidirectional' 'Z_chk2_V02' "$c\corridor_s1.xml" 'FastSim_*.csv' 300 @('-RecordPositions=1','-RecordSimTime') -Out $out
"CHECK COMPLETE" | Tee-Object $out -Append
