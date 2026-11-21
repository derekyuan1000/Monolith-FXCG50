"""Tiny helper for the patch scripts: exact, idempotent text replacements.

Each patch is a list of (old, new) pairs applied to a file of the pristine
KhiCAS tree (giacbf.tgz). A marker string tells whether the file is already
patched, so the scripts can be re-run safely."""
import sys

def patch(path, pairs, marker):
    s = open(path, encoding='latin-1').read()
    if marker in s:
        return False
    for old, new in pairs:
        if s.count(old) != 1:
            sys.exit('%s: the text to patch was not found exactly once:\n%s' % (path, old[:200]))
        s = s.replace(old, new, 1)
    open(path, 'w', encoding='latin-1').write(s)
    return True

def append(path, text, marker):
    s = open(path, encoding='latin-1').read()
    if marker in s:
        return False
    open(path, 'w', encoding='latin-1').write(s + text)
    return True
