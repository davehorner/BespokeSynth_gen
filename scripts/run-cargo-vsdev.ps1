$ErrorActionPreference = "Stop"

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vcvars = $null
$cl = $null
if (Test-Path $vswhere) {
   $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
   if ($installPath) {
      $candidate = Join-Path $installPath "VC\Auxiliary\Build\vcvars64.bat"
      if (Test-Path $candidate) {
         $vcvars = $candidate
      }
      $clCandidate = Get-ChildItem (Join-Path $installPath "VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe") -ErrorAction SilentlyContinue |
         Sort-Object FullName -Descending |
         Select-Object -First 1
      if ($clCandidate) {
         $cl = $clCandidate.FullName
      }
   }
}

if (-not $vcvars) {
   $fallback = "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
   if (Test-Path $fallback) {
      $vcvars = $fallback
   }
}

if (-not $cl) {
   $clCandidate = Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe" -ErrorAction SilentlyContinue |
      Sort-Object FullName -Descending |
      Select-Object -First 1
   if ($clCandidate) {
      $cl = $clCandidate.FullName
   }
}

if (-not $vcvars) {
   throw "Visual Studio C++ build tools were not found; install the Desktop development with C++ workload."
}

if (-not $cl) {
   throw "Visual Studio C++ compiler cl.exe was not found; install the Desktop development with C++ workload."
}

$quotedArgs = @()
for ($i = 0; $i -lt $args.Count; ++$i) {
   $arg = $args[$i]
   if ($i -eq 0 -and $arg -match '^[A-Za-z0-9_.:-]+$') {
      $quotedArgs += $arg
   } else {
      $quotedArgs += '"' + ($arg -replace '"', '""') + '"'
   }
}

$command = 'call "' + $vcvars + '" >nul && ' +
   'set "NVCC_CCBIN=' + $cl + '" && ' +
   'set "GIT_CONFIG_COUNT=1" && ' +
   'set "GIT_CONFIG_KEY_0=core.longpaths" && ' +
   'set "GIT_CONFIG_VALUE_0=true" && ' +
   ($quotedArgs -join ' ')
cmd.exe /S /C $command
exit $LASTEXITCODE
