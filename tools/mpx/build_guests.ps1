$ErrorActionPreference = "Stop"
$env:WASI_SDK_PATH = "C:\Users\PC-001\AppData\Local\MicroPixel\environments\50bc5f1b910d382bdb87c70a\wasi"
$env:WASI_CLANGXX = "$env:WASI_SDK_PATH\bin\clang++.exe"
$env:WAMRC = "H:\jl\fw-mini-sdk\tools\mpx\wamrc.exe"
$root = "H:\jl\micropixel"
$outDir = "H:\jl\fw-mini-sdk\build\guest-bundles"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Set-Location $root
$apps = @(
    @{ dir = "sdk-demo"; name = "demo" },
    @{ dir = "snake"; name = "snake" },
    @{ dir = "maze-evil"; name = "maze-break" },
    @{ dir = "blocks"; name = "blocks" },
    @{ dir = "tilt"; name = "tilt" },
    @{ dir = "tomb-explorer"; name = "tomb-explorer" }
)
foreach ($app in $apps) {
    Write-Host "==== PACKAGE $($app.dir) ===="
    python -X utf8 tools\micropixel package "guest\apps\$($app.dir)" --profile size --aot-target riscv32-ilp32f
    if ($LASTEXITCODE -ne 0) { throw "package $($app.dir) failed: $LASTEXITCODE" }
    $wasm = Join-Path $root "build\apps\$($app.name)\$($app.name).wasm"
    $pack = Join-Path $root "build\apps\$($app.name)\generated\resources.pack"
    $out = Join-Path $outDir "$($app.dir).bundle.bin"
    $py = @("-X", "utf8", "tools\build_app_bundle.py", "--app-manifest", "guest\apps\$($app.dir)\app.json", "--wasm", $wasm, "--output", $out)
    if (Test-Path $pack) { $py += @("--resource-pack", $pack) }
    python @py
    if ($LASTEXITCODE -ne 0) { throw "bundle $($app.dir) failed: $LASTEXITCODE" }
    $len = (Get-Item $out).Length
    Write-Host "BUNDLED $($app.dir) $len"
}
Write-Host "ALL_BUNDLES_OK"
