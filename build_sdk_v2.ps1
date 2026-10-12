param([ValidateSet('Debug','Release')][string]$Configuration='Release',[switch]$NoDemo,[switch]$NoVision,[switch]$Clean)
$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot
$taskBuild=Join-Path $taskRoot ('out\sdk-v2-'+$Configuration.ToLower())
$taskDevShell='D:\software\VS\Common7\Tools\Launch-VsDevShell.ps1'
if(Test-Path -LiteralPath $taskDevShell){& $taskDevShell -Arch amd64 -SkipAutomaticLocation}
$env:VSLANG='1033'
# This installation has Chinese compiler resources. Explicitly preserve the
# UTF-8 prefix so Ninja tracks header dependencies instead of stale ABI layouts.
$env:FPGA_MSVC_INCLUDE_PREFIX='注意: 包含文件:'
$taskNinja='D:\software\VS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$taskToolchain='D:\software\vcpkg\vcpkg\scripts\buildsystems\vcpkg.cmake'
$taskDemo=if($NoDemo){'OFF'}else{'ON'}
$taskVision=if($NoVision){'OFF'}else{'ON'}
$taskArgs=@('-S',$taskRoot,'-B',$taskBuild,'-G','Ninja',"-DCMAKE_BUILD_TYPE=$Configuration",'-DFPGA_BUILD_LEGACY_APP=OFF',"-DFPGA_BUILD_DEMO=$taskDemo", "-DFPGA_BUILD_VISION=$taskVision",'-DBUILD_TESTING=ON')
if(Test-Path -LiteralPath $taskNinja){$taskArgs+="-DCMAKE_MAKE_PROGRAM=$taskNinja"}
if(Test-Path -LiteralPath $taskToolchain){$taskArgs+="-DCMAKE_TOOLCHAIN_FILE=$taskToolchain"}
$taskPythonRoot='D:\CODE2\fpga_upper'
$taskPython=Join-Path $taskPythonRoot 'venv\Scripts\python.exe'
if(Test-Path -LiteralPath $taskPython){$taskArgs+="-DFPGA_PYTHON_REFERENCE_ROOT=$taskPythonRoot";$taskArgs+="-DFPGA_TEST_PYTHON=$taskPython"}
cmake @taskArgs
if($LASTEXITCODE){exit $LASTEXITCODE}
$taskBuildArgs=@('--build',$taskBuild,'--parallel')
if($Clean){$taskBuildArgs+='--clean-first'}
cmake @taskBuildArgs
if($LASTEXITCODE){exit $LASTEXITCODE}
ctest --test-dir $taskBuild --output-on-failure
if($LASTEXITCODE){exit $LASTEXITCODE}
$taskPackage=if($Configuration -eq 'Release'){'out\sdk-v2-package'}else{'out\sdk-v2-package-debug'}
cmake --install $taskBuild --prefix (Join-Path $taskRoot $taskPackage)
exit $LASTEXITCODE
