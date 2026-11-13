# ILION v1.4.0 candidate: synthetic check of the XeMFG XMX texts.
# Every Tr()/TrL() text of the XeMFG XMX panel, the plugin tabs and the XeMFG XMX option of the FG Nvngx combo must
# exist in the Spanish, Portuguese and Russian tables (ilion_lang*.h), and every translation must use only ASCII,
# Latin-1 letters and punctuation already used by the menu, or Cyrillic. Run: python -I ilion_xmx_lang_check.py
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MENU = (ROOT / "menu" / "menu_common.cpp").read_text(encoding="utf-8-sig")

LITERAL = r'"((?:[^"\\]|\\.)*)"'


def unescape(text):
    return re.sub(r'\\(.)', lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), text)


def literal_after(source, start):
    # One C string made of adjacent literals ("a" "b" ...), from start
    out, pos = [], start
    while True:
        m = re.compile(r'\s*' + LITERAL).match(source, pos)
        if not m:
            break
        out.append(unescape(m.group(1)))
        pos = m.end()
    return "".join(out)


def body(name):
    start = MENU.index(name)
    end = MENU.index("\n}\n", start)
    return MENU[start:end]


keys = set()
for fn in ("static void DrawXeMfgXmxState()", "static void DrawPluginTabs(bool xmx)"):
    text = body(fn)
    for m in re.finditer(r'\bTrL?\(', text):
        value = literal_after(text, m.end())
        if value:
            keys.add(value)

option = MENU.index("{ FGNvngxReplacement::XeMfgXmx,")
label_end = MENU.index(",", MENU.index('"', option))
keys.add(literal_after(MENU, MENU.index('"', option)))
keys.add(literal_after(MENU, label_end + 1))
keys.add("backend error: not generating")
keys.discard("XeMFG XMX")  # a product name, the same in every language


def table(path):
    source = (ROOT / "menu" / path).read_text(encoding="utf-8-sig")
    entries = {}
    for m in re.finditer(r'\{\s*((?:' + LITERAL + r'\s*)+),\s*((?:' + LITERAL + r'\s*)+)\}', source):
        en = "".join(unescape(x) for x in re.findall(LITERAL, m.group(1)))
        tr = "".join(unescape(x) for x in re.findall(LITERAL, m.group(3)))
        entries.setdefault(en, tr)
    return entries


tables = {"es": table("ilion_lang.h"), "pt": table("ilion_lang_pt.h"), "ru": table("ilion_lang_ru.h")}
SAME_WORD = {"Plugin"}  # identical in Spanish and Portuguese, the fallback keeps it


def allowed(ch):
    code = ord(ch)
    return (code < 0x80 or 0xC0 <= code <= 0xFF and code != 0xD7 and code != 0xF7 or ch in "«»¿¡·" or
            0x400 <= code <= 0x4FF)


failures = []
for key in sorted(keys):
    for lang, entries in tables.items():
        if key not in entries:
            if key in SAME_WORD and lang != "ru":
                continue
            failures.append(f"[{lang}] missing: {key[:90]}")
            continue
        bad = sorted({ch for ch in entries[key] if not allowed(ch)})
        if bad:
            failures.append(f"[{lang}] characters {bad}: {entries[key][:90]}")
        if key.count("%") != entries[key].count("%"):
            failures.append(f"[{lang}] format specifiers differ: {key[:90]}")

for line in failures:
    print(line)
print(f"ILION XMX TEXTS: {len(keys)} texts x 3 languages, {len(failures)} failures")
sys.exit(1 if failures else 0)
