# Ensure download.bat carries chip KEY for isd_download.
# fixbat.exe may drop newly-added lines once the bat has accumulated multi-CR
# endings (CRCRCR...\n). This script also normalizes line endings to CRLF so
# subsequent makes stop growing CR runs.
param(
    [Parameter(Mandatory = $true)]
    [string]$BatPath
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $BatPath)) {
    Write-Error "missing bat: $BatPath"
    exit 1
}

$keyLine = 'set KEY_FILE=-key 0x7DF1.key'
$raw = [System.IO.File]::ReadAllText($BatPath)

# Collapse CR runs then normalize to CRLF
$text = $raw -replace "`r+", "`r"
$text = $text -replace "`r`n", "`n" -replace "`n", "`r`n"

# Activate KEY on the placeholder REM line if still present
$text = [regex]::Replace(
    $text,
    '(?m)^REM set KEY_FILE=-key JL_791N-XXXX\.key\r?$',
    $keyLine
)

if ($text -notmatch '(?m)^set KEY_FILE=-key 0x7DF1\.key\r?$') {
    # Insert before CFG_FILE (always present) as last resort
    $inserted = [regex]::Replace(
        $text,
        '(?m)^(set CFG_FILE=cfg\r?$)',
        ($keyLine + "`r`n`r`n" + '$1'),
        1
    )
    if ($inserted -eq $text) {
        Write-Error "failed to inject KEY_FILE into $BatPath"
        exit 1
    }
    $text = $inserted
    Write-Host "[ensure_download_key] injected KEY_FILE before CFG_FILE"
} else {
    Write-Host "[ensure_download_key] KEY_FILE present"
}

[System.IO.File]::WriteAllText($BatPath, $text, [System.Text.UTF8Encoding]::new($false))
exit 0
