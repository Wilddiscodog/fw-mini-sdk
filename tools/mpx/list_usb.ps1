Get-Volume | Format-Table DriveLetter, FileSystemLabel, DriveType -AutoSize
Get-CimInstance Win32_PnPEntity |
    Where-Object { $_.Name -match 'CH340|COM\d+|Mass Storage' } |
    Select-Object -ExpandProperty Name
