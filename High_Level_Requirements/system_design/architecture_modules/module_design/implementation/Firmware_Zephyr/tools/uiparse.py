#!/usr/bin/env python3
"""Extract clickable elements + texts from a uiautomator dump.
Usage: py uiparse.py <ui.xml>"""
import re
import sys

xml = open(sys.argv[1], 'rb').read().decode('utf-8', 'replace')
for m in re.finditer(r'<node[^>]*/?>', xml):
    n = m.group(0)
    text = re.search(r'text="([^"]*)"', n)
    rid = re.search(r'resource-id="([^"]*)"', n)
    clickable = re.search(r'clickable="([^"]*)"', n)
    bounds = re.search(r'bounds="([^"]*)"', n)
    desc = re.search(r'content-desc="([^"]*)"', n)
    t = text.group(1) if text else ''
    r = rid.group(1) if rid else ''
    d = desc.group(1) if desc else ''
    if t or d or (clickable and clickable.group(1) == 'true'):
        print(f"text={t!r} desc={d!r} id={r!r} click={clickable.group(1) if clickable else '?'} bounds={bounds.group(1) if bounds else '?'}")
