Import("env")

from pathlib import Path

project = Path(env.subst("$PROJECT_DIR"))
src = project / "data" / "index.html"
out = project / "src" / "generated_web.h"

html = src.read_text(encoding="utf-8")
content = """#pragma once\n\nstatic const char WEB_INDEX_HTML[] = R\"OSTESTER_HTML(\n""" + html + "\n)OSTESTER_HTML\";\n"

if not out.exists() or out.read_text(encoding="utf-8") != content:
    out.write_text(content, encoding="utf-8")
    print("Generated src/generated_web.h from data/index.html")
