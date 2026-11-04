#!/usr/bin/env python3
"""Dart brace/paren/bracket balance checker (string, comment and
interpolation aware). Not a full parser — a tokenizer sanity gate for
environments without the Dart SDK. Exit 0 = balanced everywhere."""
import sys, os

IDCHARS = set('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$')

def check_path(path):
    src = open(path, encoding='utf-8').read()
    i, n = 0, len(src)
    line = 1
    mode = 'code'           # 'code' | 'str' | 'block_comment'
    stack = []              # ('d', '(') delimiters | ('i', strstate) interpolations
    strstate = None         # (quote, triple_bool, raw_bool)
    errors = []
    while i < n:
        c = src[i]
        if c == '\n':
            line += 1
        if mode == 'block_comment':
            if c == '*' and i + 1 < n and src[i+1] == '/':
                mode = 'code'
                i += 2
                continue
            i += 1
            continue
        if mode == 'str':
            quote, triple, raw = strstate
            if not raw and c == '\\' and i + 1 < n:
                i += 2
                continue
            if triple:
                if src.startswith(quote * 3, i):
                    mode = 'code'
                    i += 3
                    continue
            else:
                if c == quote:
                    mode = 'code'
                    i += 1
                    continue
            if c == '$' and i + 1 < n and src[i+1] == '{':
                stack.append(('i', strstate))
                mode = 'code'
                i += 2
                continue
            i += 1
            continue
        # code mode
        if c == '/' and i + 1 < n and src[i+1] == '/':
            j = src.find('\n', i)
            i = n if j < 0 else j
            continue
        if c == '/' and i + 1 < n and src[i+1] == '*':
            mode = 'block_comment'
            i += 2
            continue
        if c in ('"', "'"):
            raw = src[i-1:i] == 'r' and (i < 2 or src[i-2] not in IDCHARS)
            triple = src.startswith(c * 3, i)
            strstate = (c, triple, raw)
            mode = 'str'
            i += 3 if triple else 1
            continue
        if c in '([{':
            stack.append(('d', c))
            i += 1
            continue
        if c in ')]}':
            opener = {')': '(', ']': '[', '}': '{'}[c]
            if not stack:
                errors.append(f'{path}:{line}: unmatched closer {c!r}')
                i += 1
                continue
            kind, val = stack[-1]
            if kind == 'i':
                # interpolation close must be '}' and resumes the string
                if c == '}':
                    strstate = val
                    mode = 'str'
                    stack.pop()
                    i += 1
                    continue
                errors.append(f'{path}:{line}: {c!r} crosses an interpolation')
                i += 1
                continue
            if val != opener:
                errors.append(f'{path}:{line}: closer {c!r} does not match open {val!r}')
                i += 1
                continue
            stack.pop()
            i += 1
            continue
        i += 1
    if mode == 'str':
        errors.append(f'{path}: EOF inside string literal')
    if mode == 'block_comment':
        errors.append(f'{path}: EOF inside block comment')
    for kind, val in stack:
        if kind == 'd':
            errors.append(f'{path}: EOF: unmatched opener {val!r}')
        else:
            errors.append(f'{path}: EOF: unterminated interpolation')
    return errors

def main():
    roots = sys.argv[1:]
    files = []
    for r in roots:
        if os.path.isdir(r):
            for dp, _, fns in os.walk(r):
                files += [os.path.join(dp, f) for f in fns if f.endswith('.dart')]
        else:
            files.append(r)
    all_errors = []
    for f in sorted(files):
        all_errors += check_path(f)
    if all_errors:
        print('\n'.join(all_errors))
        sys.exit(1)
    print(f'OK: {len(files)} dart file(s) balanced')

if __name__ == '__main__':
    main()
