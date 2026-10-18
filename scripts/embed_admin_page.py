#!/usr/bin/env python3
"""Embed the canonical self-contained admin page; --check detects drift."""
from pathlib import Path
import json
import sys

root = Path(__file__).resolve().parents[1]
header = root / "idf_app/main/admin_page_dial.h"
old = header.read_text(encoding="utf-8")
html = (root / "web/admin_dial.html").read_text(encoding="utf-8")
prefix = old.split("static const char ADMIN_PAGE[] =")[0]
suffix = "static const char ADMIN_FORMS_JS[] =" + old.split("static const char ADMIN_FORMS_JS[] =", 1)[1]
expected = prefix + "static const char ADMIN_PAGE[] =\n" + "".join(
    json.dumps(line, ensure_ascii=False) + "\n" for line in html.splitlines(keepends=True)
) + ";\n" + suffix
if "--check" in sys.argv:
    if old != expected:
        raise SystemExit("admin_page_dial.h differs; run scripts/embed_admin_page.py")
else:
    header.write_text(expected, encoding="utf-8", newline="\n")
