import bespoke
import module
import math

# Relay-driven mpvplayer movie swarm.
#
# This script treats the video as a timeline made of many short segments. Over
# time it spawns many mpvplayer processes, each one starting at the next movie
# timestamp. The currently active segment is audible; nearby segments can remain
# visible briefly so geometry can tween and overlap without keeping every mpv
# process alive forever.

PLAYER_PREFIX = "mpv_relay_player_"
DEFAULT_URL = "https://www.youtube.com/watch?v=x2VHFgyawPE"

PROCESS_COUNT = 12
FALLBACK_MOVIE_SECONDS = 600.0
PREROLL_SECONDS = 18.0
TAIL_SECONDS = 18.0
TICK_SECONDS = 0.35
SCREEN_W = 1920.0
SCREEN_H = 1080.0
MIN_VISIBLE_PLAYERS = 2
PREROLL_PLAYERS = 1
MAX_ACTIVE_PLAYERS = 4
GEOMETRY_SECONDS = 0.70
USER_MOVE_THRESHOLD = 24.0

status_lines = []
active_players = []
elapsed_seconds = 0.0
start_measure_time = 0.0
movie_seconds = FALLBACK_MOVIE_SECONDS
next_segment_index = 0
relay_running = False


def _status(text):
   status_lines.append(str(text))
   while len(status_lines) > 12:
      status_lines.pop(0)

   this.output(text)
   bespoke.set_background_text("mpv relay swarm\n" + "\n".join(status_lines), 14, 20, 120, 0.9, 0.95, 1.0)


def _seconds_to_measures(seconds):
   beats_per_measure = bespoke.get_time_sig_ratio() * 4.0
   seconds_per_measure = 60.0 / bespoke.get_tempo() * beats_per_measure
   return max(0.01, seconds / seconds_per_measure)


def _measure_to_seconds(measures):
   beats_per_measure = bespoke.get_time_sig_ratio() * 4.0
   seconds_per_measure = 60.0 / bespoke.get_tempo() * beats_per_measure
   return measures * seconds_per_measure


def _relay_elapsed_seconds():
   return max(0.0, _measure_to_seconds(bespoke.get_measure_time() - start_measure_time))


def _clamp(value, low, high):
   return max(low, min(high, value))


def _smoothstep(value):
   value = _clamp(value, 0.0, 1.0)
   return value * value * (3.0 - 2.0 * value)


def _lerp(a, b, amount):
   return a + (b - a) * amount


def _player_name(index):
   return PLAYER_PREFIX + str(index + 1)


def _segment_seconds():
   return movie_seconds / max(1, PROCESS_COUNT)


def _movie_time_for_segment(index):
   return index * _segment_seconds()


def _relay_time_for_segment(index):
   return index * _segment_seconds()


def _segment_progress(elapsed, index):
   return _clamp((elapsed - _relay_time_for_segment(index) + PREROLL_SECONDS) / max(0.01, _segment_seconds() + PREROLL_SECONDS), 0.0, 1.0)


def _grid_rect(index):
   col = index % 2
   row = int(index / 2) % 2
   x = col * SCREEN_W * 0.5
   y = row * SCREEN_H * 0.5
   w = SCREEN_W * 0.5
   h = SCREEN_H * 0.5
   rect = dict()
   rect["x"] = x
   rect["y"] = y
   rect["w"] = w
   rect["h"] = h
   return rect


def _cover_rect(index):
   inset = 18.0 + (index % 8) * 18.0
   x = inset
   y = inset * 0.65
   w = SCREEN_W - inset * 2.0
   h = SCREEN_H - inset * 1.3
   rect = dict()
   rect["x"] = x
   rect["y"] = y
   rect["w"] = w
   rect["h"] = h
   return rect


def _strip_rect(index):
   slots = 4
   slot = index % slots
   w = SCREEN_W / slots
   x = slot * w
   y = 0.0
   h = SCREEN_H
   rect = dict()
   rect["x"] = x
   rect["y"] = y
   rect["w"] = w
   rect["h"] = h
   return rect


def _overlay_rect(index):
   base = _grid_rect(index)
   if index % 3 == 2:
      base["x"] += 110.0
      base["y"] += 70.0
      base["w"] *= 0.82
      base["h"] *= 0.82
   return base


def _cascade_rect(index):
   w = SCREEN_W * 0.48
   h = SCREEN_H * 0.50
   phase = (index % 12) / 11.0
   x = phase * (SCREEN_W - w)
   y = (1.0 - phase) * (SCREEN_H - h)
   rect = dict()
   rect["x"] = x
   rect["y"] = y
   rect["w"] = w
   rect["h"] = h
   return rect


LAYOUTS = [_grid_rect, _strip_rect, _cascade_rect, _cover_rect, _overlay_rect]


def _rect_for_segment(index, progress):
   layout_index = int(index / 5) % len(LAYOUTS)
   next_layout_index = (layout_index + 1) % len(LAYOUTS)
   rect_a = LAYOUTS[layout_index](index)
   rect_b = LAYOUTS[next_layout_index](index)
   amount = _smoothstep(progress)
   x = _lerp(rect_a["x"], rect_b["x"], amount)
   y = _lerp(rect_a["y"], rect_b["y"], amount)
   w = _lerp(rect_a["w"], rect_b["w"], amount)
   h = _lerp(rect_a["h"], rect_b["h"], amount)
   rect = dict()
   rect["x"] = x
   rect["y"] = y
   rect["w"] = w
   rect["h"] = h

   # A small shared drift keeps the relay feeling like one moving system rather
   # than unrelated windows.
   drift_x = 150.0 * math.sin((elapsed_seconds + index * 7.0) * 0.17)
   drift_y = 90.0 * math.sin((elapsed_seconds + index * 5.0) * 0.13)
   if index == _active_segment_index():
      pulse = 0.5 + 0.5 * math.sin(elapsed_seconds * 0.45)
      rect["x"] -= rect["w"] * 0.08 * pulse
      rect["y"] -= rect["h"] * 0.08 * pulse
      rect["w"] *= 1.0 + 0.16 * pulse
      rect["h"] *= 1.0 + 0.16 * pulse
   rect["x"] += drift_x
   rect["y"] += drift_y
   rect["x"] = _clamp(rect["x"], -rect["w"] * 0.5, SCREEN_W - rect["w"] * 0.5)
   rect["y"] = _clamp(rect["y"], -rect["h"] * 0.5, SCREEN_H - rect["h"] * 0.5)
   return rect


def _write_geometry(player, rect):
   player.set("x", rect["x"])
   player.set("y", rect["y"])
   player.set("w", rect["w"])
   player.set("h", rect["h"])


def _read_geometry(player):
   rect = dict()
   rect["x"] = player.get("x")
   rect["y"] = player.get("y")
   rect["w"] = player.get("w")
   rect["h"] = player.get("h")
   return rect


def _capture_user_geometry(item):
   try:
      rect = _read_geometry(item["player"])
   except Exception:
      return

   dx = rect["x"] - item["last_x"]
   dy = rect["y"] - item["last_y"]
   dw = rect["w"] - item["last_w"]
   dh = rect["h"] - item["last_h"]
   moved = abs(dx) > USER_MOVE_THRESHOLD or abs(dy) > USER_MOVE_THRESHOLD
   resized = abs(dw) > USER_MOVE_THRESHOLD or abs(dh) > USER_MOVE_THRESHOLD
   if not moved and not resized:
      return

   item["offset_x"] += dx
   item["offset_y"] += dy
   item["offset_w"] += dw
   item["offset_h"] += dh
   item["last_x"] = rect["x"]
   item["last_y"] = rect["y"]
   item["last_w"] = rect["w"]
   item["last_h"] = rect["h"]
   item["last_geometry"] = elapsed_seconds


def _apply_user_offset(item, rect):
   shifted = dict()
   shifted["x"] = rect["x"] + item["offset_x"]
   shifted["y"] = rect["y"] + item["offset_y"]
   shifted["w"] = max(64.0, rect["w"] + item["offset_w"])
   shifted["h"] = max(64.0, rect["h"] + item["offset_h"])
   return shifted


def _spawn_segment(index):
   name = _player_name(index)
   existing = module.get(name)
   if existing:
      existing.delete()

   player = module.create("mpvplayer", 260 + (index % 4) * 185, 220 + (index % 4) * 70)
   if not player:
      _status("could not create segment " + str(index + 1))
      return

   player.set_name(name)
   movie_time = _movie_time_for_segment(index)
   progress = _segment_progress(elapsed_seconds, index)
   rect = _rect_for_segment(index, progress)

   player.set("play", 0)
   player.set_text("media", DEFAULT_URL)
   _write_geometry(player, rect)
   player.set("offscreen", 0.5)
   player.set("crop", 2)
   player.set("animate", 0)
   player.set("time", movie_time)
   player.set("a", -1.0)
   player.set("b", -1.0)
   player.set("mute", 1)
   player.set("play", 1)

   item = dict()
   item["player"] = player
   item["index"] = index
   item["start"] = _relay_time_for_segment(index)
   item["end"] = _relay_time_for_segment(index) + _segment_seconds()
   item["last_sync"] = 0.0
   item["last_geometry"] = -1000.0
   item["last_x"] = rect["x"]
   item["last_y"] = rect["y"]
   item["last_w"] = rect["w"]
   item["last_h"] = rect["h"]
   item["offset_x"] = 0.0
   item["offset_y"] = 0.0
   item["offset_w"] = 0.0
   item["offset_h"] = 0.0
   active_players.append(item)
   _status("preloaded segment " + str(index + 1) + " at " + str(round(movie_time, 2)) + "s")


def _update_movie_duration_from_ipc():
   global movie_seconds

   for item in active_players:
      try:
         duration = item["player"].get("duration")
         if duration > 1.0 and abs(duration - movie_seconds) > 0.5:
            movie_seconds = duration
            _status("duration from mpv " + str(round(movie_seconds, 2)) + "s")
            return
      except Exception:
         pass


def _spawn_due_segments():
   global next_segment_index

   target_visible = MIN_VISIBLE_PLAYERS + PREROLL_PLAYERS
   while next_segment_index < PROCESS_COUNT:
      is_preroll_due = elapsed_seconds + PREROLL_SECONDS + 0.001 >= _relay_time_for_segment(next_segment_index)
      needs_visible_players = len(active_players) < target_visible
      if not is_preroll_due and not needs_visible_players:
         break
      _spawn_segment(next_segment_index)
      next_segment_index += 1


def _active_segment_index():
   index = int(elapsed_seconds / max(0.01, _segment_seconds()))
   return max(0, min(PROCESS_COUNT - 1, index))


def _audible_index():
   active_index = _active_segment_index()
   best_index = -1
   best_distance = 999999.0
   for item in active_players:
      index = item["index"]
      distance = abs(index - active_index)
      if distance < best_distance:
         best_distance = distance
         best_index = index
   return best_index


def _sort_active_players():
   active_index = _active_segment_index()
   decorated = []
   for item in active_players:
      index = item["index"]
      distance = abs(index - active_index)
      decorated.append((distance, index, item))
   decorated.sort()
   active_players[:] = [entry[2] for entry in decorated]


def _update_active_players():
   _sort_active_players()
   audible_index = _audible_index()
   survivors = []

   for item in active_players:
      player = item["player"]
      index = item["index"]
      should_trim = elapsed_seconds > item["end"] + TAIL_SECONDS and len(survivors) >= MIN_VISIBLE_PLAYERS
      should_cap = len(survivors) >= MAX_ACTIVE_PLAYERS
      if should_trim or should_cap:
         player.set("play", 0)
         player.delete()
         continue

      progress = _segment_progress(elapsed_seconds, index)
      _capture_user_geometry(item)
      rect = _apply_user_offset(item, _rect_for_segment(index, progress))
      geometry_due = elapsed_seconds - item["last_geometry"] >= GEOMETRY_SECONDS
      geometry_changed = abs(rect["x"] - item["last_x"]) > 12.0 or abs(rect["y"] - item["last_y"]) > 12.0
      geometry_changed = geometry_changed or abs(rect["w"] - item["last_w"]) > 12.0 or abs(rect["h"] - item["last_h"]) > 12.0
      if geometry_due and geometry_changed:
         _write_geometry(player, rect)
         item["last_geometry"] = elapsed_seconds
         item["last_x"] = rect["x"]
         item["last_y"] = rect["y"]
         item["last_w"] = rect["w"]
         item["last_h"] = rect["h"]
      is_audible = index == audible_index
      player.set("mute", 0 if is_audible else 1)
      if is_audible:
         expected_time = _movie_time_for_segment(index) + max(0.0, elapsed_seconds - item["start"])
         now = elapsed_seconds
         try:
            reported_time = player.get("time")
            if abs(reported_time - expected_time) > 5.0 and now - item["last_sync"] > 4.0:
               player.set("time", expected_time)
               item["last_sync"] = now
         except Exception:
            pass
      survivors.append(item)

   active_players[:] = survivors


def mpv_relay_tick():
   global elapsed_seconds
   global relay_running

   if not relay_running:
      return

   _update_movie_duration_from_ipc()
   elapsed_seconds = _relay_elapsed_seconds()

   if elapsed_seconds >= movie_seconds or next_segment_index >= PROCESS_COUNT and len(active_players) == 0:
      stop()
      _status("relay complete")
      return

   _spawn_due_segments()
   _update_active_players()
   this.schedule_call(_seconds_to_measures(TICK_SECONDS), "mpv_relay_tick()")


def start():
   global elapsed_seconds
   global start_measure_time
   global movie_seconds
   global next_segment_index
   global relay_running

   stop()
   elapsed_seconds = 0.0
   start_measure_time = bespoke.get_measure_time()
   movie_seconds = FALLBACK_MOVIE_SECONDS
   next_segment_index = 0
   relay_running = True
   _status("starting 24-process relay")
   _status(DEFAULT_URL)
   mpv_relay_tick()


def stop():
   global relay_running
   relay_running = False
   for item in active_players:
      try:
         item["player"].set("play", 0)
         item["player"].delete()
      except Exception:
         pass
   active_players[:] = []
   _status("stopped")


start()
