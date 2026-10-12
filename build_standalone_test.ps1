$ErrorActionPreference = "Stop"
& 'D:\software\VS\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -SkipAutomaticLocation
cmake --build 'd:\CODE2\FPGA_TEST\fpga_upper_cpp\out\build\x64-Debug' --target sdk_standalone_test CameraSDK
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# 确保测试 exe 从 exe 所在目录加载的是新构建的 SDK DLL（覆盖可能残留的旧副本）
Copy-Item 'd:\CODE2\FPGA_TEST\fpga_upper_cpp\out\build\x64-Debug\sdk\CameraSDK.dll' 'd:\CODE2\FPGA_TEST\fpga_upper_cpp\out\build\x64-Debug\CameraSDK.dll' -Force
Write-Output "BUILD_OK"