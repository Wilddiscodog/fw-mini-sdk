param(
    [string]$Port = 'COM19',
    [int]$Baud = 1000000,
    [string]$LogFile = 'D:\fw-mini-sdk\uart19.log'
)
$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.ReadTimeout = 200
$sp.NewLine = "`n"
try {
    $sp.Open()
} catch {
    "$(Get-Date -Format HH:mm:ss.fff) [mon] OPEN FAILED: $_" | Out-File -Append -Encoding utf8 $LogFile
    exit 1
}
"$(Get-Date -Format HH:mm:ss.fff) [mon] opened $Port @$Baud" | Out-File -Append -Encoding utf8 $LogFile
while ($true) {
    try {
        $data = $sp.ReadExisting()
        if ($data.Length -gt 0) {
            [System.IO.File]::AppendAllText($LogFile, $data, [System.Text.Encoding]::ASCII)
        } else {
            Start-Sleep -Milliseconds 20
        }
    } catch {
        "$(Get-Date -Format HH:mm:ss.fff) [mon] read err: $_" | Out-File -Append -Encoding utf8 $LogFile
        Start-Sleep -Seconds 1
    }
}
