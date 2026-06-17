import bespoke
import module

# Walk the Acuneus shader dropdown from Python.
#
# Drop this script into a script module and run it. It will find an existing
# Acuneus module or create one, then select/open each shader by dropdown index.

ACUNEUS_MODULE_NAME = "acuneus_shader_walk"
START_INDEX = 0
FALLBACK_MAX_SHADERS = 64
SECONDS_PER_SHADER = 5.0
WRAP = True
CREATE_ACUNEUS_IF_MISSING = True
SHOW_BACKGROUND_STATUS = True
MAX_STATUS_LINES = 8
HIDE_OVERLAY = True
HIDE_TITLE_BAR = True

status_lines = []
shader_count = FALLBACK_MAX_SHADERS


def _status(text):
   status_lines.append(str(text))
   while len(status_lines) > MAX_STATUS_LINES:
      status_lines.pop(0)

   this.output(text)

   if SHOW_BACKGROUND_STATUS:
      status_text = "acuneus shader walk\n" + "\n".join(status_lines)
      bespoke.set_background_text(status_text, 14, 20, 120, 0.85, 0.95, 1.0)


def _get_shader_count():
   try:
      count = int(bespoke.get_acuneus_shader_count())
      if count > 0:
         return count
   except Exception as error:
      _status("could not query shader count: " + str(error))
   return FALLBACK_MAX_SHADERS


def _find_acuneus():
   preferred = module.get(ACUNEUS_MODULE_NAME)
   if preferred:
      return preferred

   for path in bespoke.get_modules():
      lower = path.lower()
      if "acuneus" in lower:
         return module.get(path)

   if CREATE_ACUNEUS_IF_MISSING:
      acuneus = module.create("acuneus", 200, 220)
      acuneus.set_name(ACUNEUS_MODULE_NAME)
      return acuneus

   return None


def _hide_acuneus_chrome(acuneus):
   if not acuneus:
      return

   if HIDE_OVERLAY:
      acuneus.set("overlay", 0)
   if HIDE_TITLE_BAR:
      acuneus.set("title bar", 0)


def _set_shader(index):
   acuneus = _find_acuneus()
   if not acuneus:
      _status("no Acuneus module found")
      return False

   _hide_acuneus_chrome(acuneus)
   acuneus.set("shader", index)
   acuneus.set("open", 1)
   _hide_acuneus_chrome(acuneus)
   _status("shader index " + str(index))
   return True


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
         _status("done")
         return
      shader_index = START_INDEX

   if _set_shader(shader_index):
      shader_index += 1
      this.schedule_call(_seconds_to_measures(SECONDS_PER_SHADER), "acuneus_shader_walk_step()")


def start():
   global shader_index
   global shader_walk_running
   global shader_count

   acuneus = _find_acuneus()
   if acuneus:
      _hide_acuneus_chrome(acuneus)
   shader_count = _get_shader_count()
   shader_index = START_INDEX
   shader_walk_running = True
   _status("started, " + str(shader_count) + " shaders")
   acuneus_shader_walk_step()


def stop():
   global shader_walk_running

   shader_walk_running = False
   _status("stopped")


start()
