import bespoke
import module

AUTOMATION_NAME = "acuneus_automation"
ACUNEUS_MODULE_NAME = "acuneus_automation_visualizer"
STABLE_AUDIO_MODULE_NAME = "acuneus_automation_stableaudio"
CANDLE_VIDEO_MODULE_NAME = "acuneus_automation_candlevideo"
DEFAULT_OUTPUT_MODULE_NAME = "acuneus_automation_default_output"
GAIN_MODULE_NAME = "acuneus_automation_gain"
OUTPUT_MODULE_NAME = "acuneus_automation_output"

START_INDEX = 0
FALLBACK_MAX_SHADERS = 64
SECONDS_PER_SHADER = 30.0
WRAP = True
SHOW_BACKGROUND_STATUS = True
MAX_STATUS_LINES = 14
HIDE_OVERLAY = True
HIDE_TITLE_BAR = True
MAX_ACUNEUS_WINDOWS = 8
SOURCE_X = 260
ACUNEUS_X = 920
GAIN_X = 1320
OUTPUT_X = 1480
TOP_Y = 220
LAYOUT_OFFSET_X = 260
LAYOUT_OFFSET_Y = 80
AUTOMATION_ZOOM = 0.55
SOURCE_GAP_Y = 40
ACUNEUS_GAP_Y = 50
SOURCE_MIN_SPACING_Y = 170
ACUNEUS_MIN_SPACING_Y = 520
ANIMATION_SECONDS_PER_STEP = 0.15
ANIMATION_MIN_X = 0.0
ANIMATION_MIN_Y = 0.0
ANIMATION_MAX_X = 1920.0
ANIMATION_MAX_Y = 1080.0
ANIMATION_DEFAULT_WIDTH = 520.0
ANIMATION_DEFAULT_HEIGHT = 360.0

status_lines = []
shader_count = FALLBACK_MAX_SHADERS
shader_index = START_INDEX
shader_walk_running = False
animation_running = False
animation_states = {}


def _status(text):
   status_lines.append(str(text))
   while len(status_lines) > MAX_STATUS_LINES:
      status_lines.pop(0)

   this.output(text)

   if SHOW_BACKGROUND_STATUS:
      status_text = "acuneus automation\n" + "\n".join(status_lines)
      bespoke.set_background_text(status_text, 14, 20, 120, 0.85, 0.95, 1.0)


def _get_shader_count():
   try:
      count = int(bespoke.get_acuneus_shader_count())
      if count > 0:
         return count
   except Exception as error:
      _status("could not query shader count: " + str(error))
   return FALLBACK_MAX_SHADERS


def _find_named(name):
   found = module.get(name)
   if found:
      return found
   return None


def _find_or_create(module_type, name, x, y):
   found = _find_named(name)
   if found:
      found.set_position(x, y)
      return found

   created = module.create(module_type, x, y)
   if created:
      created.set_name(name)
      _status("created " + module_type + " as " + name)
   else:
      _status("could not create " + module_type)
   return created


def _get_window_count():
   try:
      count = int(round(this.get("windows")))
   except Exception:
      count = 1

   return max(1, min(MAX_ACUNEUS_WINDOWS, count))


def _animation_enabled():
   try:
      return this.get("animate") >= 0.5
   except Exception:
      return False


def _acuneus_name(index):
   if index == 0:
      return ACUNEUS_MODULE_NAME
   return ACUNEUS_MODULE_NAME + "_" + str(index + 1)


def _layout_source_x():
   try:
      return max(SOURCE_X, this.get_position_x() + LAYOUT_OFFSET_X)
   except Exception:
      return SOURCE_X


def _layout_top_y():
   try:
      return max(TOP_Y, this.get_position_y() + LAYOUT_OFFSET_Y)
   except Exception:
      return TOP_Y


def _acuneus_position(index):
   source_x = _layout_source_x()
   top_y = _layout_top_y()
   return source_x + (ACUNEUS_X - SOURCE_X), top_y + index * ACUNEUS_MIN_SPACING_Y


def _source_position(index):
   return _layout_source_x(), _layout_top_y() + index * SOURCE_MIN_SPACING_Y


def _module_height(target, default_height):
   if not target:
      return default_height
   try:
      return max(default_height, target.get_height())
   except Exception:
      return default_height


def _stack_modules(items, x, y, min_spacing, gap):
   current_y = y
   for item in items:
      if not item:
         current_y += min_spacing
         continue
      item.set_position(x, current_y)
      current_y += max(min_spacing, _module_height(item, min_spacing - gap) + gap)


def _safe_set(target, control, value):
   if target:
      target.set(control, value)


def _safe_target(source, target):
   if source and target:
      source.set_target(target)


def _safe_add_target(source, target):
   if source and target:
      source.add_target(target)


def _safe_clear_targets(source):
   if source:
      source.clear_targets()


def _hide_acuneus_chrome(acuneus):
   if not acuneus:
      return

   if HIDE_OVERLAY:
      _safe_set(acuneus, "overlay", 0)
   if HIDE_TITLE_BAR:
      _safe_set(acuneus, "title bar", 0)


def _find_acuneus(index=0):
   name = _acuneus_name(index)
   acuneus = _find_named(name)
   if acuneus:
      return acuneus

   if index == 0:
      for path in bespoke.get_modules():
         lower = path.lower()
         if "acuneus" in lower and "automation" not in lower:
            return module.get(path)

   x, y = _acuneus_position(index)
   return _find_or_create("acuneus", name, x, y)


def _get_acuneus_modules():
   acuneus_modules = []
   for index in range(_get_window_count()):
      acuneus = _find_acuneus(index)
      if acuneus:
         acuneus_modules.append(acuneus)
   return acuneus_modules


def _shader_for_window(base_index, window_index):
   index = base_index + window_index
   if index >= shader_count:
      if WRAP:
         index = START_INDEX + ((index - START_INDEX) % max(1, shader_count - START_INDEX))
      else:
         return None
   return index


def _focus_automation_layout(acuneus_modules, fallback_module):
   try:
      if acuneus_modules:
         acuneus_modules[0].set_focus(AUTOMATION_ZOOM)
      elif fallback_module:
         fallback_module.set_focus(AUTOMATION_ZOOM)
   except Exception:
      pass


def _configure_patch():
   source_x = _layout_source_x()
   top_y = _layout_top_y()
   acuneus_x = source_x + (ACUNEUS_X - SOURCE_X)
   gain_x = source_x + (GAIN_X - SOURCE_X)
   output_x = source_x + (OUTPUT_X - SOURCE_X)
   stable_audio = _find_or_create("stableaudio", STABLE_AUDIO_MODULE_NAME, source_x, top_y)
   default_output = _find_or_create("defaultoutput", DEFAULT_OUTPUT_MODULE_NAME, source_x, top_y + SOURCE_MIN_SPACING_Y)
   candle_video = _find_or_create("candlevideo", CANDLE_VIDEO_MODULE_NAME, source_x, top_y + SOURCE_MIN_SPACING_Y * 2)
   gain = _find_or_create("gain", GAIN_MODULE_NAME, gain_x, top_y)
   output = _find_or_create("output", OUTPUT_MODULE_NAME, output_x, top_y)
   acuneus_modules = _get_acuneus_modules()

   _stack_modules([stable_audio, default_output, candle_video], source_x, top_y, SOURCE_MIN_SPACING_Y, SOURCE_GAP_Y)

   _safe_clear_targets(stable_audio)
   _safe_clear_targets(default_output)
   _safe_clear_targets(candle_video)
   _safe_clear_targets(gain)

   for index, acuneus in enumerate(acuneus_modules):
      _safe_set(acuneus, "shader", index)
      _safe_set(acuneus, "music auto", 1)
      _safe_set(acuneus, "music amt", 1)
      _safe_set(acuneus, "open", 1)
      _safe_set(acuneus, "anchor", 0)
      _hide_acuneus_chrome(acuneus)
      _safe_clear_targets(acuneus)
      if index == 0:
         _safe_target(stable_audio, acuneus)
         _safe_target(default_output, acuneus)
         _safe_target(candle_video, acuneus)
      else:
         _safe_add_target(candle_video, acuneus)
      _safe_target(acuneus, gain)

   _stack_modules(acuneus_modules, acuneus_x, top_y, ACUNEUS_MIN_SPACING_Y, ACUNEUS_GAP_Y)
   _safe_target(gain, output)

   _safe_set(stable_audio, "autoplay", 1)
   _safe_set(stable_audio, "autonext", 1)
   _safe_set(stable_audio, "loop", 1)
   _safe_set(stable_audio, "play", 1)
   _safe_set(default_output, "monitor", 0)
   _safe_set(candle_video, "autoload", 1)
   _safe_set(candle_video, "autoplay", 1)

   _status("running " + str(len(acuneus_modules)) + " acuneus windows")
   _status("connected stableaudio into first acuneus")
   _status("connected default output monitor into first acuneus")
   _status("shared candlevideo across acuneus media")
   _status("connected acuneus windows through shared gain to output")
   _focus_automation_layout(acuneus_modules, gain)
   first_acuneus = acuneus_modules[0] if acuneus_modules else None
   return first_acuneus


def _set_shader(index):
   acuneus_modules = _get_acuneus_modules()
   if not acuneus_modules:
      _status("no Acuneus modules found")
      return False

   shader_indices = []
   for window_index, acuneus in enumerate(acuneus_modules):
      shader = _shader_for_window(index, window_index)
      if shader is None:
         continue

      _hide_acuneus_chrome(acuneus)
      acuneus.set("shader", shader)
      acuneus.set("open", 1)
      _hide_acuneus_chrome(acuneus)
      _apply_animation_state_to_window(window_index, acuneus)
      shader_indices.append(shader)

   if not shader_indices:
      return False

   _status("shader indices " + ", ".join(str(shader) for shader in shader_indices))
   if _animation_enabled() and not animation_running:
      start_animation()
   return len(shader_indices) > 0


def _clamp(value, low, high):
   return max(low, min(high, value))


def _animation_seed(index):
   state = {}
   state["x"] = 80.0 + index * 220.0
   state["y"] = 90.0 + index * 120.0
   state["w"] = ANIMATION_DEFAULT_WIDTH
   state["h"] = ANIMATION_DEFAULT_HEIGHT
   state["vx"] = 5.5 + index * 0.9
   state["vy"] = 3.8 + index * 0.7
   return state


def _ensure_animation_states(acuneus_modules):
   for index, acuneus in enumerate(acuneus_modules):
      name = _acuneus_name(index)
      if name not in animation_states:
         state = _animation_seed(index)
         try:
            x = acuneus.get("window x")
            y = acuneus.get("window y")
            w = acuneus.get("window w")
            h = acuneus.get("window h")
            if w > 1 and h > 1:
               state["w"] = w
               state["h"] = h
               state["x"] = _clamp(x, ANIMATION_MIN_X, ANIMATION_MAX_X - state["w"])
               state["y"] = _clamp(y, ANIMATION_MIN_Y, ANIMATION_MAX_Y - state["h"])
         except Exception:
            pass
         animation_states[name] = state


def _apply_animation_state_to_window(index, acuneus):
   if not _animation_enabled() or not acuneus:
      return

   name = _acuneus_name(index)
   if name not in animation_states:
      animation_states[name] = _animation_seed(index)
   state = animation_states.get(name)
   if not state:
      return

   try:
      current_width = acuneus.get("window w")
      current_height = acuneus.get("window h")
      if current_width > 1 and current_height > 1:
         state["w"] = current_width
         state["h"] = current_height
   except Exception:
      pass

   _safe_set(acuneus, "anchor", 0)
   _safe_set(acuneus, "window x", state["x"])
   _safe_set(acuneus, "window y", state["y"])


def _bounce_animation_state(state):
   state["x"] += state["vx"]
   state["y"] += state["vy"]

   if state["x"] <= ANIMATION_MIN_X or state["x"] + state["w"] >= ANIMATION_MAX_X:
      state["vx"] = -state["vx"]
   if state["y"] <= ANIMATION_MIN_Y or state["y"] + state["h"] >= ANIMATION_MAX_Y:
      state["vy"] = -state["vy"]

   state["x"] = _clamp(state["x"], ANIMATION_MIN_X, ANIMATION_MAX_X - state["w"])
   state["y"] = _clamp(state["y"], ANIMATION_MIN_Y, ANIMATION_MAX_Y - state["h"])


def _separate_animation_states(states):
   for left_index in range(len(states)):
      left = states[left_index]
      for right_index in range(left_index + 1, len(states)):
         right = states[right_index]
         overlap_x = min(left["x"] + left["w"], right["x"] + right["w"]) - max(left["x"], right["x"])
         overlap_y = min(left["y"] + left["h"], right["y"] + right["h"]) - max(left["y"], right["y"])
         if overlap_x <= 0 or overlap_y <= 0:
            continue

         left_cx = left["x"] + left["w"] * 0.5
         right_cx = right["x"] + right["w"] * 0.5
         left_cy = left["y"] + left["h"] * 0.5
         right_cy = right["y"] + right["h"] * 0.5
         if overlap_x < overlap_y:
            push = overlap_x * 0.5 + 2.0
            direction = -1.0 if left_cx < right_cx else 1.0
            left["x"] += direction * push
            right["x"] -= direction * push
            left["vx"], right["vx"] = -right["vx"], -left["vx"]
         else:
            push = overlap_y * 0.5 + 2.0
            direction = -1.0 if left_cy < right_cy else 1.0
            left["y"] += direction * push
            right["y"] -= direction * push
            left["vy"], right["vy"] = -right["vy"], -left["vy"]

         left["x"] = _clamp(left["x"], ANIMATION_MIN_X, ANIMATION_MAX_X - left["w"])
         right["x"] = _clamp(right["x"], ANIMATION_MIN_X, ANIMATION_MAX_X - right["w"])
         left["y"] = _clamp(left["y"], ANIMATION_MIN_Y, ANIMATION_MAX_Y - left["h"])
         right["y"] = _clamp(right["y"], ANIMATION_MIN_Y, ANIMATION_MAX_Y - right["h"])


def acuneus_animation_step():
   global animation_running

   if not animation_running:
      return

   if not _animation_enabled():
      animation_running = False
      return

   acuneus_modules = _get_acuneus_modules()
   _ensure_animation_states(acuneus_modules)
   states = []
   for index, acuneus in enumerate(acuneus_modules):
      state = animation_states[_acuneus_name(index)]
      _bounce_animation_state(state)
      states.append(state)

   _separate_animation_states(states)

   for index, acuneus in enumerate(acuneus_modules):
      state = animation_states[_acuneus_name(index)]
      current_width = acuneus.get("window w")
      current_height = acuneus.get("window h")
      if current_width > 1 and current_height > 1:
         state["w"] = current_width
         state["h"] = current_height
      _safe_set(acuneus, "window x", state["x"])
      _safe_set(acuneus, "window y", state["y"])

   this.schedule_call(_seconds_to_measures(ANIMATION_SECONDS_PER_STEP), "acuneus_animation_step()")


def start_animation():
   global animation_running

   if not _animation_enabled():
      return
   if animation_running:
      return

   animation_running = True
   _status("window animation started")
   acuneus_animation_step()


def _seconds_to_measures(seconds):
   beats_per_measure = bespoke.get_time_sig_ratio() * 4.0
   seconds_per_measure = 60.0 / bespoke.get_tempo() * beats_per_measure
   return max(0.01, seconds / seconds_per_measure)


def acuneus_shader_walk_step():
   global shader_index
   global shader_walk_running

   if not shader_walk_running:
      return

   if shader_index >= shader_count:
      if not WRAP:
         shader_walk_running = False
         _status("shader walk done")
         return
      shader_index = START_INDEX

   window_count = _get_window_count()
   if _set_shader(shader_index):
      shader_index += window_count
      this.schedule_call(_seconds_to_measures(SECONDS_PER_SHADER), "acuneus_shader_walk_step()")


def start_shader_walk():
   global shader_index
   global shader_walk_running
   global shader_count

   shader_count = _get_shader_count()
   shader_index = START_INDEX
   shader_walk_running = True
   _status("shader walk started, " + str(shader_count) + " shaders")
   acuneus_shader_walk_step()


def stop_shader_walk():
   global shader_walk_running

   shader_walk_running = False
   _status("shader walk stopped")


def start():
   global animation_running

   animation_running = False
   _status("starting automation")
   _configure_patch()
   start_animation()
   start_shader_walk()


def stop():
   global animation_running

   animation_running = False
   stop_shader_walk()


start()
