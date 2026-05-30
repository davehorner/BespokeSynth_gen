#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import tarfile
from urllib.parse import quote
from urllib.request import Request, urlopen


STABLE_AUDIO_RELEASE_BASE = "https://github.com/thewh1teagle/stableaudio-rs/releases/download/models-v0.1.0"
STABLE_AUDIO_ARCHIVES = {
    "small-music": {
        "archive": "stable-audio-3-small-music-q8_0.tar.gz",
        "files": (
            "models/gguf-q8_0/sa3-small-music-dit.gguf",
            "models/gguf-q8_0/sa3-small-music-same-s-decoder.gguf",
            "models/gguf-q8_0/t5gemma-b-b-ul2-encoder.gguf",
        ),
    },
    "small-sfx": {
        "archive": "stable-audio-3-small-sfx-q8_0.tar.gz",
        "files": (
            "models/gguf-q8_0/sa3-small-sfx-dit.gguf",
            "models/gguf-q8_0/sa3-same-s-decoder.gguf",
            "models/gguf-q8_0/t5gemma-b-b-ul2-encoder.gguf",
        ),
    },
    "medium": {
        "archive": "stable-audio-3-medium-q8_0.tar.gz",
        "dependencies": ("small-music",),
        "files": (
            "models/gguf-q8_0/sa3-medium-dit.gguf",
            "models/gguf-q8_0/sa3-medium-same-l-decoder.gguf",
            "models/gguf-q8_0/t5gemma-b-b-ul2-encoder.gguf",
        ),
    },
}

CANDLE_VIDEO_REPO = "oxide-lab/LTX-Video-0.9.8-2B-distilled"
CANDLE_VIDEO_FILES = (
    ("ltxv-2b-0.9.8-distilled.safetensors", "ltxv-2b-0.9.8-distilled.safetensors"),
    ("text_encoder_gguf/t5-v1_1-xxl-encoder-Q5_K_M.gguf", "text_encoder_gguf/t5-v1_1-xxl-encoder-Q5_K_M.gguf"),
    ("text_encoder_gguf/tokenizer.json", "text_encoder_gguf/tokenizer.json"),
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def stable_audio_root(root: Path) -> Path:
    return root


def candle_video_model_root(root: Path) -> Path:
    return root / "models" / "ltx-video"


def human_size(num_bytes: int) -> str:
    value = float(num_bytes)
    for unit in ("B", "KB", "MB", "GB"):
        if value < 1024 or unit == "GB":
            return f"{value:.1f} {unit}" if unit != "B" else f"{num_bytes} B"
        value /= 1024
    return f"{num_bytes} B"


def download_file(url: str, dest: Path, keep: bool = True) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() and dest.stat().st_size > 0:
        print(f"ready: {dest}")
        return

    tmp = dest.with_suffix(dest.suffix + ".part")
    tmp.unlink(missing_ok=True)
    print(f"download: {url}")
    print(f"     to: {dest}")

    request = Request(url, headers={"User-Agent": "BespokeSynth-model-downloader"})
    with urlopen(request) as response, tmp.open("wb") as file:
        total = int(response.headers.get("content-length", "0") or "0")
        downloaded = 0
        next_report = 0
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            file.write(chunk)
            downloaded += len(chunk)
            if total and downloaded >= next_report:
                print(f"  {human_size(downloaded)} / {human_size(total)}")
                next_report = downloaded + max(total // 20, 32 * 1024 * 1024)
            elif not total and downloaded >= next_report:
                print(f"  {human_size(downloaded)}")
                next_report = downloaded + 64 * 1024 * 1024

    tmp.replace(dest)
    if not keep:
        dest.unlink(missing_ok=True)


def safe_extract_tar_gz(archive_path: Path, dest: Path) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    dest_resolved = dest.resolve()
    print(f"extract: {archive_path}")
    with tarfile.open(archive_path, "r:gz") as archive:
        for member in archive.getmembers():
            target = (dest / member.name).resolve()
            if target != dest_resolved and dest_resolved not in target.parents:
                raise RuntimeError(f"refusing to extract unsafe path: {member.name}")
        archive.extractall(dest)


def stable_audio_model_ready(root: Path, model: str) -> bool:
    return all((root / item).exists() for item in STABLE_AUDIO_ARCHIVES[model]["files"])


def download_stable_audio_model(root: Path, model: str, keep_archives: bool, seen: set[str]) -> None:
    if model in seen:
        return
    seen.add(model)

    spec = STABLE_AUDIO_ARCHIVES[model]
    for dependency in spec.get("dependencies", ()):
        download_stable_audio_model(root, dependency, keep_archives, seen)

    if stable_audio_model_ready(root, model):
        print(f"ready: StableAudio {model}")
        return

    archive_name = spec["archive"]
    archive_path = root / archive_name
    download_file(f"{STABLE_AUDIO_RELEASE_BASE}/{archive_name}", archive_path)
    safe_extract_tar_gz(archive_path, root)
    if not keep_archives:
        archive_path.unlink(missing_ok=True)

    if not stable_audio_model_ready(root, model):
        missing = [str(root / item) for item in spec["files"] if not (root / item).exists()]
        raise RuntimeError(f"StableAudio {model} download finished but files are missing: {missing}")
    print(f"ready: StableAudio {model}")


def hf_resolve_url(repo: str, path: str, revision: str = "main") -> str:
    quoted_path = "/".join(quote(part) for part in path.split("/"))
    return f"https://huggingface.co/{repo}/resolve/{quote(revision)}/{quoted_path}"


def download_candle_video_model(dest: Path, repo: str, revision: str) -> None:
    for remote_path, local_path in CANDLE_VIDEO_FILES:
        download_file(hf_resolve_url(repo, remote_path, revision), dest / local_path)
    print(f"ready: CandleVideo LTX model at {dest}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Download BespokeSynth Rust model weights.")
    parser.add_argument("--root", type=Path, default=repo_root(), help="BespokeSynth_gen checkout root")
    parser.add_argument(
        "--stable-audio",
        choices=("none", "small-music", "small-sfx", "medium", "all"),
        default="all",
        help="StableAudio model bundle to download",
    )
    parser.add_argument("--skip-stable-audio", action="store_true", help="Do not download StableAudio models")
    parser.add_argument("--skip-candle-video", action="store_true", help="Do not download CandleVideo models")
    parser.add_argument("--candle-video-repo", default=CANDLE_VIDEO_REPO, help="Hugging Face repo for CandleVideo local weights")
    parser.add_argument("--candle-video-revision", default="main", help="Hugging Face revision for CandleVideo local weights")
    parser.add_argument("--keep-archives", action="store_true", help="Keep downloaded StableAudio tarballs")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()

    if not args.skip_stable_audio and args.stable_audio != "none":
        stable_root = stable_audio_root(root)
        models = ("small-music", "small-sfx", "medium") if args.stable_audio == "all" else (args.stable_audio,)
        seen: set[str] = set()
        for model in models:
            download_stable_audio_model(stable_root, model, args.keep_archives, seen)

    if not args.skip_candle_video:
        download_candle_video_model(candle_video_model_root(root), args.candle_video_repo, args.candle_video_revision)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
