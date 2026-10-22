$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
 & D:/Xilinx/2025.1/Vivado/bin/xvlog.bat -sv tb_isp.sv ../../isp/sc130_tonemap_yuy2.v ../../isp/raw10_to_yuy2.v *> ../../reports/isp/green_compile.log
 if($LASTEXITCODE -ne 0){throw 'compile failed'}
 & D:/Xilinx/2025.1/Vivado/bin/xelab.bat tb_isp -s isp_green *> ../../reports/isp/green_elaborate.log
 if($LASTEXITCODE -ne 0){throw 'elaboration failed'}
 & D:/Xilinx/2025.1/Vivado/bin/xsim.bat isp_green -runall *> ../../reports/isp/green_run.log
 $log=Get-Content ../../reports/isp/green_run.log -Raw
 if($LASTEXITCODE -ne 0 -or $log -notmatch 'TEST_PASS' -or $log -match 'Fatal:|Error:'){throw 'simulation did not pass'}
 $log
} finally { Pop-Location }
