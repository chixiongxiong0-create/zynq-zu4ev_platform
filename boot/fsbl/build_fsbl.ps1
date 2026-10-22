param(
 [switch]$RamTest,
 [Parameter(Mandatory=$true)][string]$SourceDirectory,
 [Parameter(Mandatory=$true)][string]$BspDirectory,
 [string]$BuildDirectory="$PSScriptRoot/build",
 [string]$Compiler='D:/Xilinx/2025.1/gnu/aarch64/nt/aarch64-none/bin/aarch64-none-elf-gcc.exe'
)
$ErrorActionPreference='Stop'
$source=(Resolve-Path -LiteralPath $SourceDirectory).Path
$bsp=(Resolve-Path -LiteralPath $BspDirectory).Path
if($RamTest){$BuildDirectory=Join-Path $BuildDirectory 'ramtest'}
$prepared=Join-Path $BuildDirectory 'source'
$prepareArgs=@('--source',$source,'--output',$prepared)
if($RamTest){$prepareArgs+='--ram-test'}
python "$PSScriptRoot/prepare_fsbl.py" @prepareArgs
if($LASTEXITCODE -ne 0){throw 'FSBL source preparation failed'}
New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null
$flags=@('-DARMA53_64','-DFSBL_DEBUG_INFO','-Os','-flto','-ffat-lto-objects','-ffunction-sections','-fdata-sections','-Wall',"-I$bsp/include","-I$prepared")
if($RamTest){$flags+='-DSC130_RAM_BOOT_TEST'}
$objects=@()
foreach($s in (Get-ChildItem $prepared -File | Where-Object {$_.Extension -in '.c','.S'})) {
 $obj=Join-Path $BuildDirectory ($s.BaseName+'.o')
 & $Compiler @flags -c $s.FullName -o $obj
 if($LASTEXITCODE -ne 0){throw "FSBL compile failed: $s"}
 $objects+=$obj
}
$elf=Join-Path $BuildDirectory 'fsbl_sc130.elf'
$map=Join-Path $BuildDirectory 'fsbl.map'
& $Compiler @flags @objects "-L$bsp/lib" "-T$prepared/lscript.ld" '-Wl,--start-group' -lxil -lxilffs -lxilsecure -lxilpm -lgcc -lc '-Wl,--end-group' -n '-Wl,--gc-sections' "-Wl,-Map=$map" -o $elf
if($LASTEXITCODE -ne 0){throw 'FSBL link failed'}
$readelf=Join-Path (Split-Path $Compiler) 'aarch64-none-elf-readelf.exe'
& $readelf -h -l -s $elf | Set-Content (Join-Path $BuildDirectory 'elf-layout.txt')
python "$PSScriptRoot/check_layout.py" $map
if($LASTEXITCODE -ne 0){throw 'FSBL OCM layout validation failed'}
Write-Output "FSBL_BUILD_PASS $elf"


