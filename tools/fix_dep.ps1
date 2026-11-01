# Escape "X:/" drive prefixes in a compiler .d file so GNU make does not
# treat the colon as a second target separator ("multiple target patterns").
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Path
)
$utf8 = New-Object System.Text.UTF8Encoding $false
$text = [IO.File]::ReadAllText($Path)
$fixed = [regex]::Replace($text, '([A-Za-z]):([/\\])', '${1}\:${2}')
if ($fixed -ne $text) {
    [IO.File]::WriteAllText($Path, $fixed, $utf8)
}
