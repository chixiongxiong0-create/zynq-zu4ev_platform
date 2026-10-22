param(
 [Parameter(Mandatory=$true)][string]$Fsbl,
 [Parameter(Mandatory=$true)][string]$Pmufw,
 [Parameter(Mandatory=$true)][string]$Bitstream,
 [Parameter(Mandatory=$true)][string]$Bl31,
 [Parameter(Mandatory=$true)][string]$UBoot,
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [string]$Bootgen='D:/Xilinx/2025.1/Vitis/bin/bootgen.bat'
)
$ErrorActionPreference='Stop'
$inputs=@($Fsbl,$Pmufw,$Bitstream,$Bl31,$UBoot)
$resolved=@($inputs | ForEach-Object { (Resolve-Path -LiteralPath $_).Path.Replace('\','/') })
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$out=(Resolve-Path -LiteralPath $OutputDirectory).Path
$bif=@"
the_ROM_image:
{
 [bootloader, destination_cpu=a53-0] "$($resolved[0])"
 [pmufw_image] "$($resolved[1])"
 [destination_device=pl] "$($resolved[2])"
 [destination_cpu=a53-0, exception_level=el-3, trustzone] "$($resolved[3])"
 [destination_cpu=a53-0, exception_level=el-2] "$($resolved[4])"
}
"@
$bifPath=Join-Path $out 'boot.bif'
[System.IO.File]::WriteAllText($bifPath,$bif,[System.Text.UTF8Encoding]::new($false))
& $Bootgen -arch zynqmp -image $bifPath -o (Join-Path $out 'BOOT.BIN') -w on
if ($LASTEXITCODE -ne 0) { throw 'Bootgen failed' }
if ((Get-Item (Join-Path $out 'BOOT.BIN')).Length -gt 0x00c00000) { throw 'BOOT.BIN overlaps FIT at 12 MiB' }
