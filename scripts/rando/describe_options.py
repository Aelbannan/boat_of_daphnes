#!/usr/bin/env python3
"""wwrando's options as JSON, for the Randomizer menu (milestone R4).

    python scripts/rando/describe_options.py OUT.json

Runs under the embedded Python 3.12 (scripts/windows/rando_runtime.py), like generate_seed.py, and reads
wwrando's own option table (options/wwrando_options.py: names, types, defaults, choices, ranges, descriptions,
permalink/hidden/unbeatable flags) plus the "#region" comments that group them. The app builds the
Randomizer header from this file (windows/src/randomizer_menu.cpp), so the menu follows wwrando's options.
Descriptions are wwrando's text with its HTML reduced to plain lines.
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from dataclasses import MISSING
from enum import Enum
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
RUNTIME = Path(os.environ.get("RANDO_RUNTIME") or REPO / "build" / ("windows" if os.name == "nt" else "linux") / "rando_runtime")

if sys.version_info[:2] != (3, 12):
    py = RUNTIME / "python" / ("python.exe" if os.name == "nt" else "bin/python3")
    if not py.is_file():
        sys.exit(f"rando runtime not found at {RUNTIME}; run: python scripts/windows/rando_runtime.py")
    sys.exit(subprocess.run([str(py), str(Path(__file__).resolve()), *sys.argv[1:]]).returncode)

for _p in (RUNTIME / "wwrando", RUNTIME / "gclib", RUNTIME / "site-packages"):
    if str(_p) not in sys.path:
        sys.path.insert(0, str(_p))

from options.wwrando_options import Options  # noqa: E402


def plain(html: str) -> str:
    text = re.sub(r"<br\s*/?>", "\n", html)
    text = re.sub(r"<[^>]+>", "", text)
    return text.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").strip()


def regions() -> list[tuple[int, str]]:
    """(line, region name) from the option file's #region comments."""
    src = (RUNTIME / "wwrando" / "options" / "wwrando_options.py").read_text(encoding="utf-8").splitlines()
    return [(i, m.group(1).strip()) for i, line in enumerate(src) if (m := re.match(r"\s*#region\s+(.*)", line))]


def region_of(name: str, marks: list[tuple[int, str]], src: list[str]) -> str:
    for i, line in enumerate(src):
        if re.match(rf"\s*{re.escape(name)}\s*:", line):
            current = ""
            for at, region in marks:
                if at <= i:
                    current = region
            return current
    return ""


def ui_layout() -> dict[str, dict]:
    """Each option's label, group box and tab in wwrando's window (wwr_ui/randomizer_window.ui): a widget
    named after the option, its text or a QLabel named label_for_<option>, inside QGroupBoxes and tabs."""
    import xml.etree.ElementTree as ET
    root = ET.parse(RUNTIME / "wwrando" / "wwr_ui" / "randomizer_window.ui").getroot()
    out: dict[str, dict] = {}
    labels: dict[str, str] = {}

    def text_of(w) -> str:
        for p in w.findall("property"):
            if p.get("name") in ("text", "title"):
                s = p.find("string")
                return (s.text or "") if s is not None else ""
        return ""

    def walk(node, tab: str, groups: list[str]):
        for child in node:
            if child.tag == "widget":
                name = child.get("name", "")
                cls = child.get("class", "")
                t, g = tab, groups
                attr = child.find("attribute[@name='title']/string")
                if attr is not None:
                    t = attr.text or ""
                if cls == "QGroupBox":
                    g = groups + [text_of(child)]
                if name.startswith("label_for_"):
                    labels[name[len("label_for_"):]] = text_of(child)
                else:
                    out.setdefault(name, {"label": text_of(child), "group": g[-1] if g else "", "tab": t})
                walk(child, t, g)
            else:
                walk(child, tab, groups)

    walk(root, "", [])
    for name, label in labels.items():
        if name in out and not out[name]["label"]:
            out[name]["label"] = label
    return out


def jsonable(v):
    if isinstance(v, Enum):
        return v.value
    if isinstance(v, (list, tuple)):
        return [jsonable(x) for x in v]
    return v


def main() -> int:
    out = Path(sys.argv[1])
    src = (RUNTIME / "wwrando" / "options" / "wwrando_options.py").read_text(encoding="utf-8").splitlines()
    marks = regions()
    layout = ui_layout()
    items = []
    for opt in Options.all():
        ui = layout.get(opt.name, {})
        t = opt.type
        default = opt.default_factory() if opt.default is MISSING else opt.default
        entry = {
            "name": opt.name,
            "region": region_of(opt.name, marks, src),
            "label": ui.get("label") or opt.name.replace("_", " ").title(),
            "group": ui.get("group", ""),
            "tab": ui.get("tab", ""),
            "description": plain(opt.description),
            "permalink": opt.permalink,
            "hidden": opt.hidden,
            "unbeatable": opt.unbeatable,
        }
        if t is bool:
            entry.update(type="bool", default=bool(default))
        elif t is int:
            entry.update(type="int", default=int(default), min=opt.minimum, max=opt.maximum)
        elif isinstance(t, type) and issubclass(t, Enum):
            entry.update(type="choice", default=jsonable(default), choices=[c.value for c in t],
                         choice_descriptions={jsonable(k): plain(v) for k, v in opt.choice_descriptions.items()})
        else:
            entry.update(type="list", default=jsonable(default))
        items.append(entry)
    # Our own setting, not wwrando's (scripts/rando/generate_seed.py take_bunny_hood): off by default.
    items.append({
        "name": "bunny_hood", "region": "Modes", "label": "Add the Bunny Hood", "group": "Item Randomizer Modes",
        "tab": "Randomizer Settings",
        "description": "Adds the Bunny Hood from Majora's Mask to the item pool: one more optional item, like the "
                       "Hero's Charm, placed where any non-progress item can be. Worn from the Hero's Charm's slot "
                       "(hold A and press left or right there to swap), it makes you run 50% faster.",
        "permalink": False, "hidden": False, "unbeatable": False, "type": "bool", "default": False,
    })
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"schema": "bod.rando-options", "version": 1, "options": items}, indent=1),
                   encoding="utf-8")
    print(f"{len(items)} options -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
