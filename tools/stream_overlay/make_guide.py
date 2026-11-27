"""docs/STREAM_OVERLAY_GUIDE.md as the plain text the players get ("Stream overlay guide.txt": in game/ next to the launcher, opened by the
launcher's "How to use it in OBS" button). The .txt in docs/ is made by:  python tools/stream_overlay/make_guide.py --write
--check says whether it is up to date. The packaging scripts call guide_text() themselves."""
import os
import re
import sys
import textwrap

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
GUIDE_MD = os.path.join(ROOT, 'docs', 'STREAM_OVERLAY_GUIDE.md')
GUIDE_TXT = os.path.join(ROOT, 'docs', 'STREAM_OVERLAY_GUIDE.txt')


def guide_text():
    raw = []
    for line in open(GUIDE_MD, encoding='ascii').read().split('\n'):
        line = line.rstrip()

        if raw and line.strip() and raw[-1].strip() and not line.startswith('#') and not raw[-1].startswith('#') \
                and not re.match(r'\s*(- |\d+\. )', line):
            raw[-1] += ' ' + line.strip()
        else:
            raw.append(line)
    out = []
    for line in raw:
        line = re.sub(r'`([^`]*)`', r'\1', line)
        m = re.match(r'(#+) (.*)', line)
        if m:
            title = m.group(2)
            under = '=' if len(m.group(1)) == 1 else '-'
            out += ['', title, under * len(title), '']
            continue
        m = re.match(r'(\s*)((?:- |\d+\. ))?(.*)', line)
        indent, bullet, body = m.group(1), m.group(2) or '', m.group(3)
        if not body:
            out.append('')
            continue
        out += textwrap.wrap(body, 100, initial_indent=indent + bullet, subsequent_indent=indent + ' ' * len(bullet),
                             break_long_words=False, break_on_hyphens=False) or ['']
    return re.sub(r'\n{3,}', '\n\n', '\n'.join(out)).strip('\n') + '\n'


if __name__ == '__main__':
    if '--write' in sys.argv:
        with open(GUIDE_TXT, 'w', encoding='ascii', newline='\n') as f:
            f.write(guide_text())
        print('wrote', GUIDE_TXT)
    elif '--check' in sys.argv:
        ok = os.path.isfile(GUIDE_TXT) and open(GUIDE_TXT, encoding='ascii', newline='').read() == guide_text()
        print('guide: ok' if ok else 'guide: docs/STREAM_OVERLAY_GUIDE.txt is out of date: python tools/stream_overlay/make_guide.py --write')
        sys.exit(0 if ok else 1)
    else:
        sys.stdout.write(guide_text())
