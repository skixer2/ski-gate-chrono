#!/usr/bin/env python3
"""Find java.exe candidates for JAVA_HOME. Usage: py findjava.py"""
import os

cands = []
for root in (r'C:\Program Files', r'C:\Program Files (x86)',
             r'C:\Users\v17ni\AppData\Local\Programs',
             r'C:\Users\v17ni\AppData\Local',
             r'C:\ncs', r'C:\Android'):
    for dirpath, dirnames, _ in os.walk(root):
        if dirpath.count(os.sep) > 6:
            dirnames[:] = []
            continue
        if os.path.exists(os.path.join(dirpath, 'bin', 'java.exe')):
            cands.append(dirpath)
            dirnames[:] = []
        else:
            dirnames[:] = [d for d in dirnames
                           if d.lower() in ('java', 'jdk', 'jbr', 'bin',
                                            'android', 'android studio',
                                            'jetbrains', 'openjdk',
                                            'temurin', 'zulu', 'graalvm',
                                            'microsoft', 'programs')]
print('\n'.join(cands[:10]) if cands else 'NONE')
