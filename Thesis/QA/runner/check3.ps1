. "$PSScriptRoot\lib.ps1"
$out = "$R\check3.out"; $c = "$R\cfg"
foreach ($s in 1..3) {
  Run-One '/Game/UseCases/UrbanPlanning/MallA' "Z3_mallA_food_s$s" "$c\FoodCounter_cap1_s$s.xml" 'Infrastructure_MallA_*.csv' 600 @('-RecordSimTime','-RecordPositions=5') -Out $out
}
Run-One '/Game/UseCases/UrbanPlanning/MallB' 'Z3_mallB_s1' "$c\mall_auto_s1.xml" 'Infrastructure_MallB_*.csv' 600 @('-RecordSimTime','-RecordPositions=5') -Out $out
Run-One '/Game/UseCases/DailyUrbanLife' 'Z3_dul_s1' "$c\dul_s1.xml" 'AccurateTime_*DailyUrbanLife*.csv' 900 @('-RecordSimTime') -Out $out
Run-One '/Game/UseCases/Evacuation' 'Z3_evac_50' "$c\evac_s1.xml" 'Evacuation_*.csv' 900 @('-StressAgents=50','-RecordSimTime') -Out $out
Run-One '/Game/UseCases/Validation/V02_Bidirectional' 'Z3_V02' "$c\corridor_s1.xml" 'AccurateTime_*.csv' 300 @('-RecordPositions=1','-RecordSimTime') -Out $out
"CHECK COMPLETE" | Tee-Object $out -Append
