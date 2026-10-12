Write-Output "== CameraSDK protocol_handler obj =="
Get-ChildItem 'd:\CODE2\FPGA_TEST\fpga_upper_cpp\out\build\x64-Debug\sdk\CMakeFiles\CameraSDK.dir' -Recurse -Filter 'protocol_handler.cpp.obj' | ForEach-Object { Write-Output ("{0}  {1}" -f $_.LastWriteTime, $_.FullName) }
Write-Output "== sdk_standalone_test obj =="
Get-ChildItem 'd:\CODE2\FPGA_TEST\fpga_upper_cpp\out\build\x64-Debug\sdk_standalone_test.dir' -Recurse -Filter '*.obj' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
Write-Output "== source ts =="
(Get-Item 'd:\CODE2\FPGA_TEST\fpga_upper_cpp\communication\protocol_handler.cpp').LastWriteTime