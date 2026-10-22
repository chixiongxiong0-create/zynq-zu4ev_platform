$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
 & D:/Xilinx/2025.1/Vivado/bin/xvlog.bat -sv tb_write_completeness.sv ../../rtl/vdma_write_guard_v2.v *> compile.log
 if($LASTEXITCODE -ne 0){throw 'compile failed'}
 & D:/Xilinx/2025.1/Vivado/bin/xelab.bat tb_write_completeness -s dma_completeness *> elaborate.log
 if($LASTEXITCODE -ne 0){throw 'elaboration failed'}
 & D:/Xilinx/2025.1/Vivado/bin/xsim.bat dma_completeness -runall *> run.log
 $log=Get-Content run.log -Raw
 if($LASTEXITCODE -ne 0 -or $log -notmatch 'TEST_PASS' -or $log -match 'Fatal:|Error:'){throw 'simulation did not pass'}
 $log
} finally { Pop-Location }
