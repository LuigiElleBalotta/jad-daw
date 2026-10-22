"""Cleans the traced one-colour glyphs of assets/icons/logic-derived for use in the UI: one white fill (the QML colourizes them), a viewBox, numbers rounded to one decimal.
Usage: python -I -X utf8 tools/clean_traced_icons.py"""
import re, pathlib

SRC = pathlib.Path('assets/icons/logic-derived')
DST = pathlib.Path('ui/qml/icons')
ICONS = {
    'PrefsIcon512x512@2x': 'icon-prefs',
    'WaveformIcon512x512@2x': 'icon-waveform',
    'MidiIcon512x512@2x': 'icon-midi',
    'AnalysisIcon512x512@2x': 'icon-analysis',
    'SettingsIcon512x512@2x': 'icon-settings',
    'PatternIcon512x512@2x': 'icon-pattern',
    'IOLabelIcon512x512@2x': 'icon-io-labels',
    'KeyCmdIcon512x512@2x': 'icon-key-commands',
    'LogicSongIcon512x512@2x': 'icon-keyboard',
    'SamplerIcon512x512@2x': 'icon-sampler',
}

def rnd(m):
    v = round(float(m.group(0)), 1)
    return str(int(v)) if v == int(v) else str(v)

for name, out in ICONS.items():
    s = (SRC / (name + '.svg')).read_text(encoding='utf-8')
    w, h = re.search(r'width="(\d+)" height="(\d+)"', s).groups()
    paths = re.findall(r'<path [^>]*/>', s)
    body = []
    for p in paths:
        p = re.sub(r'fill="#[0-9A-Fa-f]{6}"', 'fill="#ffffff"', p)
        p = re.sub(r'-?\d+\.\d+', rnd, p)
        body.append(p)
    svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %s %s" width="%s" height="%s">\n%s\n</svg>\n' % (w, h, w, h, '\n'.join(body))
    (DST / (out + '.svg')).write_text(svg, encoding='utf-8')
    print(out, len(svg))
