#!/usr/bin/env python3
"""List fork hunks that an upstream edit to a neighbouring line would turn into a merge conflict.

For every hunk in `git diff <merge-base> <rev>` this rewrites the upstream line directly above and
directly below it in a synthetic "upstream" copy and runs `git merge-file` (histogram and myers).
A fork insertion with no unchanged upstream line between it and an upstream statement conflicts
the day upstream edits that statement. A blank line the fork added itself is NOT a separator.

Usage: .github/scripts/fork_merge_risk.py [--all] [upstream-ref] [rev] [-- path...]
  upstream-ref  default: upstream-nightly (git fetch upstream refs/tags/nightly-builds:refs/tags/upstream-nightly)
  rev           default: HEAD
  --all         also report neighbours that rarely change (braces, namespace, #include lines)
"""
import os
import re
import subprocess
import sys
import tempfile

STABLE = re.compile(r'^\s*([{}()\[\];,]*|namespace\b.*|}\s*//.*|#\s*include\b.*|using namespace\b.*)\s*$')


def git(*args):
    return subprocess.run(['git', *args], capture_output=True, text=True, errors='replace')


def main():
    args = sys.argv[1:]
    show_all = '--all' in args
    args = [a for a in args if a != '--all']
    paths = args[args.index('--') + 1:] if '--' in args else []
    refs = args[:args.index('--')] if '--' in args else args
    upstream = refs[0] if len(refs) > 0 else 'upstream-nightly'
    rev = refs[1] if len(refs) > 1 else 'HEAD'

    base = git('merge-base', rev, upstream).stdout.strip()
    if not base:
        sys.exit(f'no merge base between {rev} and {upstream}')
    files = paths or git('diff', '--name-only', base, rev, '--', 'src').stdout.split()
    tmp = tempfile.mkdtemp()
    names = [os.path.join(tmp, n) for n in ('ours', 'base', 'theirs')]

    def conflicts(ours, base_text, theirs):
        for name, text in zip(names, (ours, base_text, theirs)):
            with open(name, 'w') as fh:
                fh.write(text)
        return [alg for alg in ('histogram', 'myers')
                if subprocess.run(['git', 'merge-file', '-p', '--diff-algorithm=' + alg, *names],
                                  capture_output=True).returncode != 0]

    found = 0
    for f in files:
        b, o = git('show', f'{base}:{f}'), git('show', f'{rev}:{f}')
        if b.returncode or o.returncode:
            continue  # fork-only or deleted file: nothing upstream can conflict with
        lines = b.stdout.split('\n')
        diff = git('diff', '-U0', '--diff-algorithm=histogram', base, rev, '--', f).stdout
        for m in re.finditer(r'^@@ -(\d+)(?:,(\d+))? \+\d+(?:,\d+)? @@', diff, re.M):
            start, count = int(m.group(1)), int(m.group(2) or 1)
            neighbours = (start, start + 1) if count == 0 else (start - 1, start + count)
            for side, ln in zip(('above', 'below'), neighbours):
                if not 1 <= ln <= len(lines) or not lines[ln - 1].strip():
                    continue
                if not show_all and STABLE.match(lines[ln - 1]):
                    continue
                theirs = lines[:]
                theirs[ln - 1] += ' /* upstream edit */'
                hit = conflicts(o.stdout, b.stdout, '\n'.join(theirs))
                if hit:
                    found += 1
                    print(f'{f}:{start} edit of the upstream line {side} conflicts '
                          f'[{",".join(hit)}]: {lines[ln - 1].strip()[:90]}')
    print(f'{found} conflict-prone neighbour(s)', file=sys.stderr)


if __name__ == '__main__':
    main()
