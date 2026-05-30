#!/usr/bin/env sh
set -eu

sync_dep() {
  name="$1"
  repo="$2"
  ref="$3"
  path="$4"

  if [ ! -d "$path/.git" ]; then
    mkdir -p "$(dirname "$path")"
    git clone "$repo" "$path"
  else
    git -c "safe.directory=$(pwd)/$path" -C "$path" remote set-url origin "$repo"
  fi

  git -c "safe.directory=$(pwd)/$path" -C "$path" fetch --depth 1 origin "$ref"
  git -c "safe.directory=$(pwd)/$path" -C "$path" checkout --detach "$ref"
  printf "%s ready at %.12s\n" "$name" "$ref"
}

sync_dep acuneus "$1" "$2" libs/rust/acuneus
sync_dep stableaudio-rs "$3" "$4" libs/rust/stableaudio-rs
sync_dep candle-video "$5" "$6" libs/rust/candle-video
