param(
    [string]$Port = 'COM44',
    [string]$LogFile = 'D:\fw-mini-sdk\cdc44.log'
)
$sp = New-Object System.IO.Ports.SerialPort($Port)
$sp.ReadTimeout = 200
$sp.DtrEnable = $true
$sp.RtsEnable = $true
try {
    $sp.Open()
} catch {
    "$(Get-Date -Format HH:mm:ss.fff) [cdc] OPEN FAILED: $_" | Out-File -Append -Encoding utf8 $LogFile
    exit 1
}
"$(Get-Date -Format HH:mm:ss.fff) [cdc] opened $Port" | Out-File -Append -Encoding utf8 $LogFile
while ($true) {
    try {
        $data = $sp.ReadExisting()
        if ($data.Length -gt 0) {
            [System.IO.File]::AppendAllText($LogFile, $data, [System.Text.Encoding]::ASCII)
        } else {
            Start-Sleep -Milliseconds 20
        }
    } catch {
        Start-Sleep -Seconds 1
    }
}
