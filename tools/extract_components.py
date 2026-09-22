#!/usr/bin/env python3
"""Move each A2uiRenderer::Render* method out of a2ui-renderer.cpp into its own
renderer/components/<name>.cpp (verbatim — the methods stay A2uiRenderer members, so
behaviour is unchanged and the screenshot regression must stay pixel-identical)."""
import os, re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC  = os.path.join(ROOT, "src/renderer/a2ui-renderer.cpp")
OUT  = os.path.join(ROOT, "src/renderer/components")
os.makedirs(OUT, exist_ok=True)

MAP = {
    "RenderText": "text", "RenderFlexContainer": "flex-container", "RenderCard": "card",
    "RenderButton": "button", "RenderImage": "image", "RenderDivider": "divider",
    "RenderTextField": "text-field", "RenderCheckBox": "check-box",
    "RenderChoicePicker": "choice-picker", "RenderSlider": "slider",
    "RenderDateTimeInput": "date-time-input", "RenderProgressBar": "progress-bar",
    "RenderVideo": "video", "RenderAudioPlayer": "audio-player", "RenderTabs": "tabs",
    "RenderModal": "modal", "RenderList": "list", "RenderIcon": "icon", "RenderView": "view",
}

src = open(SRC).read()

def block_end(s, i):
    """Return index past the matching '}' for the first '{' at/after i, string/comment aware."""
    n = len(s)
    while i < n and s[i] != '{':
        i += 1
    depth = 0; instr = None; esc = False; com = None
    while i < n:
        c = s[i]
        if com == '//':
            if c == '\n': com = None
            i += 1; continue
        if com == '/*':
            if c == '*' and i+1 < n and s[i+1] == '/': com = None; i += 2; continue
            i += 1; continue
        if instr:
            if esc: esc = False
            elif c == '\\': esc = True
            elif c == instr: instr = None
            i += 1; continue
        if c == '/' and i+1 < n and s[i+1] == '/': com = '//'; i += 2; continue
        if c == '/' and i+1 < n and s[i+1] == '*': com = '/*'; i += 2; continue
        if c in '"\'': instr = c; i += 1; continue
        if c == '{': depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0: return i + 1
        i += 1
    return -1

spans = []          # (start, end, method)
files = {}          # filebase -> [method_text, ...]
for method, base in MAP.items():
    m = re.search(r'(?:Dali::Ui::)?View\s+A2uiRenderer::' + re.escape(method) + r'\s*\(', src)
    if not m:
        print("NOT FOUND:", method); continue
    end = block_end(src, m.end())
    assert end > 0, method
    spans.append((m.start(), end, method))
    files.setdefault(base, []).append(src[m.start():end])

# FindFirstActionInSubtree (anon-namespace helper) is only used by RenderCard → move to card.cpp
fm = re.search(r'std::pair<const TreeNode\*, std::string>\s*\nFindFirstActionInSubtree\s*\(', src)
helper = ""
if fm:
    fend = block_end(src, fm.end())
    helper = src[fm.start():fend]
    spans.append((fm.start(), fend, "FindFirstActionInSubtree"))

HEAD = '#include "renderer/render-internal.h"\n\nnamespace A2ui\n{\n\n'
TAIL = '\n} // namespace A2ui\n'
for base, methods in files.items():
    body = ""
    if base == "card" and helper:
        body += "namespace\n{\n" + helper + "\n} // namespace\n\n"
    body += "\n\n".join(methods)
    open(os.path.join(OUT, base + ".cpp"), "w").write(HEAD + body + TAIL)

# remove extracted spans from the source, back to front
for start, end, _ in sorted(spans, key=lambda s: s[0], reverse=True):
    src = src[:start] + src[end:]
open(SRC, "w").write(src)

print(f"extracted {len(spans)} blocks into {len(files)} files:")
for base in sorted(files):
    print("  src/renderer/components/%s.cpp" % base)
