param([Parameter(Mandatory=$true)][string]$OutputPath,[int]$Seconds=120,[string]$StopFile='')
$ErrorActionPreference='Stop'
$port=[IO.Ports.SerialPort]::new('COM13',115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
try {
    $port.Open()
    $end=[DateTime]::UtcNow.AddSeconds($Seconds)
    while([DateTime]::UtcNow -lt $end) {
        if($StopFile -and (Test-Path -LiteralPath $StopFile)){break}
        $data=$port.ReadExisting()
        if($data){[IO.File]::AppendAllText($OutputPath,$data)}
        Start-Sleep -Milliseconds 100
    }
} finally {if($port.IsOpen){$port.Close()}}
