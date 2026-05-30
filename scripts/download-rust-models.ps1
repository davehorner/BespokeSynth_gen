$ErrorActionPreference = "Stop"

$Script = Join-Path $PSScriptRoot "download-rust-models.py"

if (Get-Command py -ErrorAction SilentlyContinue) {
   & py -3 $Script @args
   exit $LASTEXITCODE
}

if (Get-Command python -ErrorAction SilentlyContinue) {
   & python $Script @args
   exit $LASTEXITCODE
}

throw "Python 3 is required to download Rust model weights."
