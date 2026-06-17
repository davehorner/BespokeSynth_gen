param(
   [Parameter(Mandatory = $true)] [string] $AcuneusRepo,
   [Parameter(Mandatory = $true)] [string] $AcuneusRef,
   [Parameter(Mandatory = $true)] [string] $AwispRepo,
   [Parameter(Mandatory = $true)] [string] $AwispRef,
   [Parameter(Mandatory = $true)] [string] $StableAudioRepo,
   [Parameter(Mandatory = $true)] [string] $StableAudioRef,
   [Parameter(Mandatory = $true)] [string] $CandleVideoRepo,
   [Parameter(Mandatory = $true)] [string] $CandleVideoRef
)

$ErrorActionPreference = "Stop"

function Run-Git($Arguments, $WorkingPath = $null) {
   if ($WorkingPath) {
      $FullPath = (Resolve-Path $WorkingPath).Path
      & git -c "safe.directory=$FullPath" -C $WorkingPath @Arguments
   } else {
      & git @Arguments
   }
   if ($LASTEXITCODE -ne 0) {
      throw "git $($Arguments -join ' ') exited $LASTEXITCODE"
   }
}

function Sync-Dep($Name, $Repo, $Ref, $Path) {
   if (!(Test-Path (Join-Path $Path ".git"))) {
      New-Item -ItemType Directory -Force -Path (Split-Path $Path -Parent) | Out-Null
      Run-Git @("clone", $Repo, $Path)
   } else {
      Run-Git @("remote", "set-url", "origin", $Repo) $Path
   }

   Run-Git @("fetch", "--depth", "1", "origin", $Ref) $Path
   Run-Git @("checkout", "-B", $Ref, "FETCH_HEAD") $Path
   Write-Host ($Name + " ready at latest " + $Ref)
}

Sync-Dep "acuneus" $AcuneusRepo $AcuneusRef "libs/rust/acuneus"
Sync-Dep "awisp" $AwispRepo $AwispRef "libs/rust/awisp"
Sync-Dep "stableaudio-rs" $StableAudioRepo $StableAudioRef "libs/rust/stableaudio-rs"
Sync-Dep "candle-video" $CandleVideoRepo $CandleVideoRef "libs/rust/candle-video"
