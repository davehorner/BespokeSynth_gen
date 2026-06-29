import math
import bespoke
import module
import mpvplayer

AUTOMATION_NAME = "mpv_player_automation_demo"
PLAYER_PREFIX = "mpv_player_automation_demo_player_"
DEFAULT_URL = "https://www.youtube.com/watch?v=SYBc8X2IxqM"

SCREEN_W = 1920.0
SCREEN_H = 1080.0
MAX_STATUS_LINES = 12
TIMELINE_TICK_SECONDS = 0.25
SEEK_COOLDOWN_SECONDS = 2.0
SEEK_DRIFT_SECONDS = 2.0
MOVE_AMOUNT = 0.08

status_lines = []
timeline_running = False
reserve_enabled = True
layout_mode = 0
last_seek_seconds = -9999.0


def _status(text):
   status_lines.append(str(text))
   while len(status_lines) > MAX_STATUS_LINES:
      status_lines.pop(0)

   this.output(text)
   bespoke.set_background_text("mpv player automation demo\n" + "\n".join(status_lines), 14, 20, 120, 0.9, 0.95, 1.0)


def _clamp(value, low, high):
   return max(low, min(high, value))


def _seconds_to_measures(seconds):
   beats_per_measure = bespoke.get_time_sig_ratio() * 4.0
   seconds_per_measure = 60.0 / max(1.0, bespoke.get_tempo()) * beats_per_measure
   return max(0.01, seconds / seconds_per_measure)


def _measure_to_seconds(measures):
   beats_per_measure = bespoke.get_time_sig_ratio() * 4.0
   seconds_per_measure = 60.0 / max(1.0, bespoke.get_tempo()) * beats_per_measure
   return measures * seconds_per_measure


def _timeline_seconds():
   return max(0.0, _measure_to_seconds(bespoke.get_measure_time()))


def _control_int(name, fallback):
   try:
      return int(this.get(name))
   except Exception:
      return fallback


def _control_bool(name, fallback):
   try:
      return this.get(name) >= 0.5
   except Exception:
      return fallback


def _player_count():
   return _clamp(_control_int("players", 4), 1, 16)


def _layout():
   return _clamp(_control_int("layout", layout_mode), 0, 2)


def _reserve():
   return _control_bool("reserve", reserve_enabled)


def _timeline_checkbox():
   return _control_bool("timeline", True)


def _new_rect(x, y, w, h):
   rect = {}
   rect["x"] = x
   rect["y"] = y
   rect["w"] = w
   rect["h"] = h
   return rect


def _grid_rect(index, count):
   cols = int(math.ceil(math.sqrt(max(1, count))))
   rows = int(math.ceil(float(count) / float(max(1, cols))))
   col = index % cols
   row = int(index / cols)
   w = SCREEN_W / float(max(1, cols))
   h = SCREEN_H / float(max(1, rows))
   return _new_rect(col * w, row * h, w, h)


def _strip_rect(index, count):
   if count <= 4:
      w = SCREEN_W / float(max(1, count))
      return _new_rect(index * w, 0.0, w, SCREEN_H)

   rows = 2
   cols = int(math.ceil(float(count) / float(rows)))
   col = index % cols
   row = int(index / cols)
   w = SCREEN_W / float(max(1, cols))
   h = SCREEN_H / float(rows)
   return _new_rect(col * w, row * h, w, h)


def _cascade_rect(index, count):
   inset = 34.0
   span = max(1, count - 1)
   w = SCREEN_W * 0.56
   h = SCREEN_H * 0.56
   x = inset + (SCREEN_W - w - inset * 2.0) * (float(index) / float(span))
   y = inset + (SCREEN_H - h - inset * 2.0) * (float(index) / float(span))
   return _new_rect(x, y, w, h)


def _base_rect(index, count):
   mode = _layout()
   if mode == 1:
      return _strip_rect(index, count)
   if mode == 2:
      return _cascade_rect(index, count)
   return _grid_rect(index, count)


def _player_name(index):
   return PLAYER_PREFIX + str(index + 1)


def _find_or_create_player(index):
   name = _player_name(index)
   player = mpvplayer.get(name)
   if player:
      return player

   rect = _base_rect(index, _player_count())
   player = mpvplayer.create(name, 360.0 + index * 22.0, 160.0 + index * 22.0)
   if player:
      player.open_media(DEFAULT_URL, False)
      player.set_geometry(rect["x"], rect["y"], int(rect["w"]), int(rect["h"]))
      player.set_mute(True)
      _status("created " + name)
   return player


def _players():
   count = _player_count()
   players = []
   for index in range(count):
      player = _find_or_create_player(index)
      if player:
         players.append(player)
   return players


def _animated_rect(index, count, seconds):
   rect = _base_rect(index, count)
   if not _reserve():
      return rect

   phase = seconds * 0.35 + index * 1.7
   dx = math.sin(phase) * rect["w"] * MOVE_AMOUNT
   dy = math.cos(phase * 0.71) * rect["h"] * MOVE_AMOUNT
   return _new_rect(rect["x"] + dx, rect["y"] + dy, rect["w"], rect["h"])


def _apply_layout(players, seconds):
   count = max(1, len(players))
   for index in range(count):
      rect = _animated_rect(index, count, seconds)
      players[index].set_geometry(rect["x"], rect["y"], int(rect["w"]), int(rect["h"]))


def reserve_windows(enabled=1):
   global reserve_enabled
   reserve_enabled = enabled != 0
   _apply_layout(_players(), _timeline_seconds())
   _status("reserved mpv windows" if reserve_enabled else "released reserved mpv windows")


def set_layout_mode(mode):
   global layout_mode
   layout_mode = _clamp(int(mode), 0, 2)
   _apply_layout(_players(), _timeline_seconds())
   _status("mpv layout mode " + str(layout_mode))


def _sync_playback(players, seconds):
   global last_seek_seconds

   count = max(1, len(players))
   audible = int(seconds / 8.0) % count
   may_seek = seconds - last_seek_seconds >= SEEK_COOLDOWN_SECONDS

   for index in range(count):
      player = players[index]
      player.set_play(True)
      player.set_mute(index != audible)

      if may_seek:
         try:
            drift = abs(player.get_time() - seconds)
            if drift > SEEK_DRIFT_SECONDS:
               player.set_time(seconds)
               last_seek_seconds = seconds
         except Exception:
            pass


def timeline_step():
   if not timeline_running:
      return

   seconds = _timeline_seconds()
   players = _players()
   if len(players) == 0:
      this.schedule_call(_seconds_to_measures(TIMELINE_TICK_SECONDS), "timeline_step()")
      return

   _apply_layout(players, seconds)
   _sync_playback(players, seconds)
   this.schedule_call(_seconds_to_measures(TIMELINE_TICK_SECONDS), "timeline_step()")


def start_timeline():
   global timeline_running
   timeline_running = True
   _status("mpv timeline automation running")
   timeline_step()


def stop():
   global timeline_running
   timeline_running = False
   _status("mpv timeline automation stopped")


def start():
   _status("mpv automation demo ready")
   reserve_windows(1 if _reserve() else 0)
   if _timeline_checkbox():
      start_timeline()


start()
