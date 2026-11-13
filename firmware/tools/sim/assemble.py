#!/usr/bin/env python3
"""Builds the simulator page from simulator.html, sim_core.js and the .wasm.

    assemble.py simulator.html sim_core.js lwave_sim.wasm OUT_DIR

Writes two files:
  lwave-sim.html           complete page to open from disk (needed for USB / MIDI)
  lwave-sim.fragment.html  the same page without <html>/<head>/<body>, for hosting
                           on claude.ai (the host adds the document skeleton)
"""
import base64
import os
import sys

template, core_js, wasm, out_dir = sys.argv[1:5]
html = open(template, encoding="utf-8").read()
core = open(core_js, encoding="utf-8").read()
b64 = base64.b64encode(open(wasm, "rb").read()).decode("ascii")
for marker in ("__LWAVE_WASM_BASE64__", "__LWAVE_SIMCORE__"):
    if marker not in html:
        sys.exit(f"placeholder {marker} not found in template")
fragment = html.replace("__LWAVE_SIMCORE__", core).replace("__LWAVE_WASM_BASE64__", b64)

full = (
    "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, viewport-fit=cover\">\n"
    "</head>\n<body>\n" + fragment + "\n</body>\n</html>\n"
)
os.makedirs(out_dir, exist_ok=True)
open(os.path.join(out_dir, "lwave-sim.html"), "w", encoding="utf-8").write(full)
open(os.path.join(out_dir, "lwave-sim.fragment.html"), "w", encoding="utf-8").write(fragment)
print(f"wrote {out_dir}/lwave-sim.html and lwave-sim.fragment.html ({len(full) // 1024} KB)")
