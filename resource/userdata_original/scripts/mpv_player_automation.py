import math
import bespoke
import bespoke_reserve_space
import module
import mpvplayer

AUTOMATION_NAME = "mpv_player_automation"
PLAYER_PREFIX = "mpv_player_automation_player_"
RESERVE_MODULE_NAME = "mpv_player_automation_reserve"
DEFAULT_URL = globals().get("DEFAULT_URL", "https://www.youtube.com/watch?v=SYBc8X2IxqM")

SCREEN_W = 5120.0
SCREEN_H = 1440.0
MAX_STATUS_LINES = 12
SEGMENT_POLL_SECONDS = 1.0
PLAYER_WARMUP_LAYOUT_TICKS = 8
PREROLL_SECONDS = 14.0

status_lines = []
timeline_running = False
reserve_enabled = True
layout_mode = 0
last_segment_index = -1
last_audio_key = ""
last_transport_playing = None
last_layout_key = ""
layout_warmups = {}
reserve_module = None
segments = []


def _add_segment(start, end, visible, layout, audible):
   segment = {}
   segment["start"] = start
   segment["end"] = end
   segment["visible"] = visible
   segment["layout"] = layout
   segment["audible"] = audible
   segments.append(segment)


_add_segment(0.0, 8.0, 1, 2, 0)
_add_segment(8.0, 16.0, 2, 0, 1)
_add_segment(16.0, 24.0, 3, 1, 0)
_add_segment(24.0, 32.0, 4, 0, 2)
_add_segment(32.0, 40.0, 4, 1, 3)
_add_segment(40.0, 48.0, 5, 0, 1)
_add_segment(48.0, 56.0, 6, 1, 4)
_add_segment(56.0, 64.0, 6, 0, 5)
_add_segment(64.0, 72.0, 5, 2, 2)
_add_segment(72.0, 80.0, 4, 0, 0)
_add_segment(80.0, 88.0, 4, 1, 1)
_add_segment(88.0, 96.0, 3, 2, 2)
_add_segment(96.0, 104.0, 5, 0, 3)
_add_segment(104.0, 112.0, 6, 1, 0)
_add_segment(112.0, 120.0, 4, 0, 2)
_add_segment(120.0, 128.0, 3, 2, 1)
_add_segment(128.0, 131.0, 1, 2, 0)
_add_segment(131.0, 999999.0, 1, 2, 0)


def _status(text):
   status_lines.append(str(text))
   while len(status_lines) > MAX_STATUS_LINES:
      status_lines.pop(0)

   this.output(text)
   bespoke.set_background_text("mpv player automation\n" + "\n".join(status_lines), 14, 20, 120, 0.9, 0.95, 1.0)


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


def _reserve_module():
   global reserve_module
   if reserve_module:
      return reserve_module

   try:
      reserve_module = bespoke_reserve_space.get_or_create(RESERVE_MODULE_NAME, 40.0, 390.0)
   except Exception as error:
      _status("reserve module unavailable: " + str(error))
      reserve_module = None
   return reserve_module


def _work_area():
   reserve = _reserve_module()
   if reserve:
      try:
         return _new_rect(reserve.get_video_x(), reserve.get_video_y(), reserve.get_video_w(), reserve.get_video_h())
      except Exception as error:
         _status("reserve rect unavailable: " + str(error))

   return _new_rect(1360.0, 24.0, SCREEN_W - 1384.0, SCREEN_H - 48.0)


def _work_key():
   work = _work_area()
   return str(int(work["x"])) + ":" + str(int(work["y"])) + ":" + str(int(work["w"])) + ":" + str(int(work["h"]))


def _clamp_rect_to_work(rect):
   work = _work_area()
   rect["w"] = max(1.0, min(rect["w"], work["w"]))
   rect["h"] = max(1.0, min(rect["h"], work["h"]))
   rect["x"] = _clamp(rect["x"], work["x"], work["x"] + work["w"] - rect["w"])
   rect["y"] = _clamp(rect["y"], work["y"], work["y"] + work["h"] - rect["h"])
   return rect


def _hidden_rect():
   work = _work_area()
   return _new_rect(work["x"] + work["w"] + 8192.0, work["y"] + work["h"] + 8192.0, 160.0, 90.0)


def _grid_rect(index, count):
   work = _work_area()
   cols = int(math.ceil(math.sqrt(max(1, count))))
   rows = int(math.ceil(float(count) / float(max(1, cols))))
   col = index % cols
   row = int(index / cols)
   w = work["w"] / float(max(1, cols))
   h = work["h"] / float(max(1, rows))
   return _clamp_rect_to_work(_new_rect(work["x"] + col * w, work["y"] + row * h, w, h))


def _strip_rect(index, count):
   work = _work_area()
   if count <= 4:
      w = work["w"] / float(max(1, count))
      return _clamp_rect_to_work(_new_rect(work["x"] + index * w, work["y"], w, work["h"]))

   rows = 2
   cols = int(math.ceil(float(count) / float(rows)))
   col = index % cols
   row = int(index / cols)
   w = work["w"] / float(max(1, cols))
   h = work["h"] / float(rows)
   return _clamp_rect_to_work(_new_rect(work["x"] + col * w, work["y"] + row * h, w, h))


def _cascade_rect(index, count):
   work = _work_area()
   inset = 34.0
   span = max(1, count - 1)
   w = work["w"] * 0.56
   h = work["h"] * 0.56
   x = work["x"] + inset + (work["w"] - w - inset * 2.0) * (float(index) / float(span))
   y = work["y"] + inset + (work["h"] - h - inset * 2.0) * (float(index) / float(span))
   return _clamp_rect_to_work(_new_rect(x, y, w, h))


def _segment_for_time(seconds):
   if len(segments) == 0:
      return None

   for segment in segments:
      if seconds >= segment["start"] and seconds < segment["end"]:
         return segment
   return segments[len(segments) - 1]


def _segment_index(segment):
   for index in range(len(segments)):
      if segments[index] is segment:
         return index
   return -1


def _seconds_until_next_segment(seconds, segment_index):
   if segment_index < 0 or segment_index >= len(segments):
      return SEGMENT_POLL_SECONDS

   segment = segments[segment_index]
   remaining = segment["end"] - seconds
   if remaining <= 0.0:
      return SEGMENT_POLL_SECONDS
   return _clamp(remaining + 0.05, 0.25, SEGMENT_POLL_SECONDS)


def _segment_visible_count(segment):
   if segment and "visible" in segment:
      return _clamp(int(segment["visible"]), 1, 16)
   return _player_count()


def _visible_player_count(segment):
   if segment:
      return _segment_visible_count(segment)
   return _player_count()


def _segment_audible_index(segment, count):
   audible = 0
   if segment and "audible" in segment:
      audible = segment["audible"]
   return _clamp(int(audible), 0, max(0, count - 1))


def _segment_layout(segment):
   return _clamp(_control_int("layout", layout_mode), 0, 2)


def _base_rect(index, count, segment=None):
   mode = _segment_layout(segment)
   if mode == 1:
      return _strip_rect(index, count)
   if mode == 2:
      return _cascade_rect(index, count)
   return _grid_rect(index, count)


def _player_name(index):
   return PLAYER_PREFIX + str(index + 1)


def _find_or_create_player(index, segment=None, seconds=0.0):
   name = _player_name(index)
   player = mpvplayer.get(name)
   if player:
      return player

   rect = _base_rect(index, _segment_visible_count(segment), segment)
   player = mpvplayer.create(name, rect["x"], rect["y"])
   if player:
      player.set_time(seconds)
      player.open_media(DEFAULT_URL, True)
      player.set_geometry(rect["x"], rect["y"], int(rect["w"]), int(rect["h"]))
      player.set_mute(True)
      player.set_volume(0.0)
      layout_warmups[name] = PLAYER_WARMUP_LAYOUT_TICKS
      _schedule_player_catchup(index, 2.0)
      _schedule_player_catchup(index, 6.0)
      _schedule_player_catchup(index, 12.0)
      _status("created " + name)
   return player


def _preload_player(index, seconds):
   name = _player_name(index)
   player = mpvplayer.get(name)
   if player:
      return player

   rect = _hidden_rect()
   player = mpvplayer.create(name, rect["x"], rect["y"])
   if player:
      player.set_time(seconds)
      player.open_media(DEFAULT_URL, True)
      player.set_geometry(rect["x"], rect["y"], int(rect["w"]), int(rect["h"]))
      player.set_mute(True)
      player.set_volume(0.0)
      layout_warmups[name] = PLAYER_WARMUP_LAYOUT_TICKS
      _schedule_player_catchup(index, 2.0)
      _schedule_player_catchup(index, 6.0)
      _schedule_player_catchup(index, 12.0)
      _status("preloading " + name)
   return player


def _players(count=None, segment=None, seconds=0.0):
   if count is None:
      count = _player_count()
   players = []
   for index in range(count):
      player = _find_or_create_player(index, segment, seconds)
      if player:
         players.append(player)
   return players


def _preload_upcoming_players(seconds, segment_index):
   if segment_index < 0:
      return

   current_segment = segments[segment_index]
   current_count = _visible_player_count(current_segment)
   max_count = current_count

   for index in range(segment_index + 1, len(segments)):
      segment = segments[index]
      if segment["start"] - seconds > PREROLL_SECONDS:
         break
      max_count = max(max_count, _visible_player_count(segment))

   for index in range(current_count, max_count):
      _preload_player(index, seconds)


def _apply_layout(players, segment=None):
   count = max(1, len(players))
   for index in range(count):
      rect = _base_rect(index, count, segment)
      players[index].set_geometry(rect["x"], rect["y"], int(rect["w"]), int(rect["h"]))
      name = _player_name(index)
      if name in layout_warmups:
         layout_warmups[name] = layout_warmups[name] - 1
         if layout_warmups[name] <= 0:
            del layout_warmups[name]


def _schedule_player_catchup(index, delay_seconds):
   this.schedule_call(_seconds_to_measures(delay_seconds), "_catchup_player(" + str(index) + ")")


def _catchup_player(index):
   player = mpvplayer.get(_player_name(index))
   if not player:
      return

   seconds = _timeline_seconds()
   try:
      player_time = player.get_time()
   except Exception:
      player_time = -999999.0

   if abs(player_time - seconds) > 2.5:
      try:
         player.set_time(seconds)
      except Exception:
         pass


def reserve_windows(enabled=1):
   global reserve_enabled
   reserve_enabled = enabled != 0
   seconds = _timeline_seconds()
   segment = _segment_for_time(seconds)
   _apply_layout(_players(_visible_player_count(segment), segment, seconds), segment)
   _status("reserved mpv windows" if reserve_enabled else "released reserved mpv windows")


def set_layout_mode(mode):
   global layout_mode
   layout_mode = _clamp(int(mode), 0, 2)
   seconds = _timeline_seconds()
   segment = _segment_for_time(seconds)
   _apply_layout(_players(_visible_player_count(segment), segment, seconds), segment)
   _status("mpv layout mode " + str(layout_mode))


def set_default_url(url, restart=1):
   global DEFAULT_URL
   DEFAULT_URL = str(url)
   if restart != 0:
      players = mpvplayer.get_all()
      for player in players:
         try:
            player.set_time(_timeline_seconds())
            player.open_media(DEFAULT_URL, True)
         except Exception:
            pass
   _status("mpv url " + DEFAULT_URL)


def _sync_playback(players, seconds, segment):
   global last_audio_key

   count = max(1, len(players))
   audible_index = _segment_audible_index(segment, count)
   audio_key = str(count) + ":" + str(audible_index)
   if audio_key == last_audio_key:
      return

   last_audio_key = audio_key

   for player in mpvplayer.get_all():
      try:
         player.set_mute(True)
         player.set_volume(0.0)
      except Exception:
         pass

   for index in range(count):
      player = players[index]
      is_audible = index == audible_index
      player.set_mute(not is_audible)
      player.set_volume(100.0 if is_audible else 0.0)


def _transport_playing():
   try:
      return not bespoke.is_audio_paused()
   except Exception:
      try:
         return bespoke.is_transport_playing()
      except Exception:
         return True


def _sync_transport_play_state():
   global last_transport_playing

   playing = _transport_playing()
   if last_transport_playing is not None and playing == last_transport_playing:
      return

   last_transport_playing = playing
   for player in mpvplayer.get_all():
      try:
         player.set_play(playing)
      except Exception:
         pass
   _status("mpv windows playing" if playing else "mpv windows paused")


def _layout_key(players, segment):
   return str(len(players)) + ":" + str(_segment_layout(segment)) + ":" + str(_reserve()) + ":" + _work_key()


def _apply_layout_if_changed(players, segment):
   global last_layout_key

   key = _layout_key(players, segment)
   if key == last_layout_key and len(layout_warmups) == 0:
      return

   last_layout_key = key
   _apply_layout(players, segment)


def timeline_step():
   global last_segment_index

   if not timeline_running:
      return

   _sync_transport_play_state()
   seconds = _timeline_seconds()
   segment = _segment_for_time(seconds)
   segment_index = _segment_index(segment)
   _preload_upcoming_players(seconds, segment_index)
   players = _players(_visible_player_count(segment), segment, seconds)

   if segment_index == last_segment_index:
      _apply_layout_if_changed(players, segment)
      _sync_playback(players, seconds, segment)
      delay = _seconds_until_next_segment(seconds, segment_index)
      this.schedule_call(_seconds_to_measures(delay), "timeline_step()")
      return

   last_segment_index = segment_index
   if segment:
      _status("segment " + str(segment_index) + " audible=" + str(segment["audible"]) + " visible=" + str(segment["visible"]))

   if len(players) > 0:
      _apply_layout_if_changed(players, segment)
      _sync_playback(players, seconds, segment)

   delay = _seconds_until_next_segment(seconds, segment_index)
   this.schedule_call(_seconds_to_measures(delay), "timeline_step()")


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
   _status("mpv automation ready")
   _status("default url " + DEFAULT_URL)
   if _timeline_checkbox():
      start_timeline()
   else:
      reserve_windows(1 if _reserve() else 0)


start()
