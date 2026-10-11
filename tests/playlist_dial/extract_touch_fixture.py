"""Extract unmodified production input routing for the combined worker fixture."""
from pathlib import Path
import sys
s = Path('idf_app/main/platform_display_idf.c').read_text()
parts = [s[s.index('// Swipe gesture detection'):s.index('// LVGL tick timer (')]]
for start, end in [('static void lvgl_touch_read_cb(', '\nbool platform_display_init'), ('void platform_display_process_pending(', '\nvoid platform_display_cancel_input'), ('void platform_display_playlist_transition(', None)]:
    a = s.index(start)
    b = s.index(end, a) if end else len(s)
    parts.append(s[a:s.rfind('}', a, b)+1])
Path(sys.argv[1]).write_text('\n'.join(parts))
