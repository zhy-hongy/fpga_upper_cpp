Write-Output "== exe dir =="
Get-ChildItem 'd:\CODE2\FPGA_TEST\fpga_upper_cpp\out\build\x64-Debug\sdk_standalone_test.exe' | ForEach-Object { Write-Output ("{0}  {1}" -f $_.LastWriteTime, $_.FullName) }
Write-Output "== CameraSDK.dll copies =="
Get-ChildItem 'd:\CODE2\FPGA_TEST\fpga_upper_cpp\out\build\x64-Debug' -Recurse -Filter 'CameraSDK.dll' | ForEach-Object { Write-Output ("{0}  {1}" -f $_.LastWriteTime, $_.FullName) }