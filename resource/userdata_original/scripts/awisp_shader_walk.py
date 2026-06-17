import bespoke
import module

# Walk the Awisp shader dropdown from Python.
#
# Drop this script into a script module and run it. It will find an existing
# Awisp module or create one, then select each embedded shader by dropdown index.

AWISP_MODULE_NAME = "awisp_shader_walk_visualizer"
START_INDEX = 0
FALLBACK_MAX_SHADERS = 64
SECONDS_PER_SHADER = 5.0
WRAP = True
CREATE_AWISP_IF_MISSING = True
SHOW_BACKGROUND_STATUS = True
MAX_STATUS_LINES = 8
HIDE_TITLE_BAR = True

status_lines = []
shader_index = START_INDEX
shader_count = FALLBACK_MAX_SHADERS
shader_walk_running = False


def _status(text):
   status_lines.append(str(text))
   while len(status_lines) > MAX_STATUS_LINES:
      status_lines.pop(0)

   this.output(text)

   if SHOW_BACKGROUND_STATUS:
      status_text = "awisp shader walk\n" + "\n".join(status_lines)
      bespoke.set_background_text(status_text, 14, 20, 120, 0.85, 0.95, 1.0)


def _find_awisp():
   preferred = module.get(AWISP_MODULE_NAME)
   if preferred:
      return preferred

   if CREATE_AWISP_IF_MISSING:
      awisp = module.create("awisp", 200, 220)
      awisp.set_name(AWISP_MODULE_NAME)
      _status("created Awisp")
      return awisp

   return None


def _hide_awisp_chrome(awisp):
   if not awisp:
      return

   if HIDE_TITLE_BAR:
      awisp.set("title bar", 0)


def _set_shader(index):
   awisp = _find_awisp()
   if not awisp:
      _status("no Awisp module found")
      return False

   try:
      _hide_awisp_chrome(awisp)
      awisp.set("shader", index)
      _hide_awisp_chrome(awisp)
   except Exception as error:
      _status("shader index " + str(index) + " failed: " + str(error))
      return False

   _status("shader index " + str(index))
   return True


def _seconds_to_measures(seconds):
   beats_per_measure = bespoke.get_time_sig_ratio() * 4.0
   seconds_per_measure = 60.0 / bespoke.get_tempo() * beats_per_measure
   return max(0.01, seconds / seconds_per_measure)


def awisp_shader_walk_step():
   global shader_index
   global shader_walk_running

   if not shader_walk_running:
      return

   if shader_index >= shader_count:
      if not WRAP:
         shader_walk_running = False
         _status("done")
         return
      shader_index = START_INDEX

   if _set_shader(shader_index):
      shader_index += 1
   else:
      shader_index = START_INDEX if WRAP else shader_count

   this.schedule_call(_seconds_to_measures(SECONDS_PER_SHADER), "awisp_shader_walk_step()")


def start():
   global shader_index
   global shader_walk_running

   awisp = _find_awisp()
   if awisp:
      _hide_awisp_chrome(awisp)
   shader_index = START_INDEX
   shader_walk_running = True
   _status("started")
   awisp_shader_walk_step()


def stop():
   global shader_walk_running

   shader_walk_running = False
   _status("stopped")


start()
