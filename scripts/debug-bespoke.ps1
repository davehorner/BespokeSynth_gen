param(
   [string]$BuildDir = "i/build",
   [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Get-Location).Path
$exe = Join-Path $repoRoot "$BuildDir\Source\BespokeSynth_artefacts\$Config\BespokeSynth_gen.exe"
if (!(Test-Path $exe)) {
   throw "Executable not found: $exe"
}

$programFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
$devenv = $null

if (Test-Path $vswhere) {
   $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.CoreEditor -property installationPath
   if ($installPath) {
      $candidate = Join-Path $installPath "Common7\IDE\devenv.exe"
      if (Test-Path $candidate) {
         $devenv = $candidate
      }
   }
}

if ($devenv) {
   Write-Host "Launching under Visual Studio debugger: $exe"
   & $devenv /debugexe $exe
   exit $LASTEXITCODE
}

$jit = Join-Path $env:WINDIR "System32\vsjitdebugger.exe"
if (Test-Path $jit) {
   Write-Host "Visual Studio IDE was not found. Starting app, then attaching JIT debugger: $exe"
   $process = Start-Process -FilePath $exe -PassThru
   & $jit -p $process.Id
   exit $LASTEXITCODE
}

Write-Host "No Visual Studio debugger found. Launching Debug build normally: $exe"
& $exe
