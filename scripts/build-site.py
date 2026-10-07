#!/usr/bin/env python3
"""Builds the web demo into site/: the page (web/), the WebAssembly renderer
(build-wasm/), the examples, and parts-index.json, which tells the page where
every part's files are on GitHub, at the commits this checkout pins.

  scripts/build-site.py [--cli build/fritzing-render] [--wasm build-wasm] [--out site]

Needs scripts/fetch-vendor.sh to have run (the index is made from vendor/ by
the CLI's `index` command) and the web build (build-wasm/).
"""

import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path
from urllib.parse import quote

ROOT = Path(__file__).resolve().parent.parent
FONTS = [  # Fritzing's fonts under the names part drawings and labels use
    ('Droid Sans', 'DroidSans/DroidSans.ttf', 'normal'),
    ('DroidSans', 'DroidSans/DroidSans.ttf', 'normal'),
    ('Droid Sans', 'DroidSans/DroidSans-Bold.ttf', 'bold'),
    ('Droid Sans Mono', 'DroidSans/DroidSansMono.ttf', 'normal'),
    ('DroidSansMono', 'DroidSans/DroidSansMono.ttf', 'normal'),
    ('Noto Sans', 'NotoSans/NotoSans-Regular.ttf', 'normal'),
    ('OCRA', 'OCRA.ttf', 'normal'),
    ('OCR A Std', 'OCRA.ttf', 'normal'),
    ('OCR-Fritzing-mono', 'OCR-Fritzing-mono.ttf', 'normal'),
]
VENDOR = ROOT / 'vendor'
LIBS = ROOT / 'libraries'


def sha(path):
    return subprocess.run(['git', '-C', str(path), 'rev-parse', 'HEAD'], check=True, capture_output=True, text=True).stdout.strip()


def raw(repo, commit, path=''):
    return f'https://raw.githubusercontent.com/{repo}/{commit}/' + '/'.join(quote(p) for p in path.split('/') if p)


def archives(library, *, bins=False, top=None):
    """{unpacked folder name: archive path in the library}, as fetch-vendor.sh names them."""
    out = {}
    base = library / top if top else library
    # a top folder is unpacked flat (its .fzpz files only), a library whole
    for z in sorted(base.glob('*') if top else base.rglob('*')):
        if '.git' in z.parts or z.suffix not in ('.fzpz', '.fzbz') or (z.suffix == '.fzbz' and not bins):
            continue
        rel = z.relative_to(base).as_posix()
        folder = z.stem if top else rel.replace('/', '_')[: -len(z.suffix)]
        out[folder] = z.relative_to(library).as_posix()
    return out


def sources():
    """Per library root: where its files come from. Loose roots map paths to a
    raw.githubusercontent.com base; archive roots map a part's folder to the
    .fzpz (or .fzbz) it was unpacked from."""
    s = {}
    s['fritzing-parts'] = {'base': raw('fritzing/fritzing-parts', sha(VENDOR / 'fritzing-parts'))}
    s['fritzing-parts-extra'] = {'base': raw('LeonFedotov/fritzing-parts-extra', sha(LIBS / 'fritzing-parts-extra'))}
    s['elegoo-parts'] = {'base': raw('marcinwisniowski/ElegooFritzingBin', sha(LIBS / 'elegoo'), 'bin') + '/'}
    for root, repo, lib, opts in [
        ('adafruit-parts', 'adafruit/Fritzing-Library', 'adafruit', {'top': 'parts'}),
        ('sparkfun-parts', 'sparkfun/Fritzing_Parts', 'sparkfun', {'top': 'products'}),
        ('seeed-parts', 'Seeed-Studio/fritzing_parts', 'seeed', {'bins': True}),
        ('mgesteiro-parts', 'mgesteiro/fritzing-parts', 'mgesteiro', {}),
        ('dip-ic-parts', 'Adr-hyng/74LS-Series-Fritzing-Parts', 'dip-ics', {}),
        ('mkjanke-parts', 'mkjanke/Fritzing-Parts', 'mkjanke', {}),
    ]:
        commit = sha(LIBS / lib)
        s[root] = {'archives': {folder: raw(repo, commit, path) for folder, path in archives(LIBS / lib, **opts).items()}}
    # community parts, as fetch-vendor.sh fetches them
    tder = VENDOR / 'community-src-tder'
    community = {z.stem: raw('TD-er/fritzing-parts', sha(tder), z.relative_to(tder).as_posix()) for z in tder.glob('*/*.fzpz')}
    script = (ROOT / 'scripts/fetch-vendor.sh').read_text()
    for folder, marker in [('DOIT Esp32 DevKit v1 improved', 'jorgechacblogspot'), ('Feather S2', 'FeatherS2-Fritzing')]:
        url = next(line.strip().strip('"') for line in script.splitlines() if marker in line and 'https://' in line)
        community[folder] = url[url.index('https://'):]
    s['community-parts'] = {'archives': community}
    return s


def index(cli):
    parts = json.loads(subprocess.run([cli, 'index'], check=True, capture_output=True, text=True).stdout)
    src = sources()
    archive_urls, out, skipped = [], [], 0
    for p in parts:
        root = Path(p['root']).name
        if root not in src:
            skipped += 1
            continue
        e = {'t': p['title'], 'm': p['moduleId'], 'f': p['family'], 'r': root, 'p': p['fzp']}
        if p['breadboard']:
            e['b'] = p['breadboard']
        if p['schematic']:
            e['s'] = p['schematic']
        if 'archives' in src[root]:
            folder = p['fzp'].split('/')[0]
            url = src[root]['archives'].get(folder)
            if url is None:
                skipped += 1
                continue
            if url not in archive_urls:
                archive_urls.append(url)
            e['z'] = archive_urls.index(url)
        out.append(e)
    roots = {name: {'base': v['base'].rstrip('/') + '/'} for name, v in src.items() if 'base' in v}
    return {'roots': roots, 'archives': archive_urls, 'parts': out}, skipped


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cli', default=str(ROOT / 'build/fritzing-render'))
    ap.add_argument('--wasm', default=str(ROOT / 'build-wasm'))
    ap.add_argument('--out', default=str(ROOT / 'site'))
    ap.add_argument('--fritzing-app', default=str(ROOT.parent / 'fritzing-app'))
    args = ap.parse_args()
    out = Path(args.out)
    shutil.rmtree(out, ignore_errors=True)
    shutil.copytree(ROOT / 'web', out)
    for f in ['fritzing-render-web.js', 'fritzing-render-web.wasm', 'fritzing-render-web.data', 'qtloader.js']:
        shutil.copy(Path(args.wasm) / f, out / f)
    shutil.copytree(ROOT / 'examples', out / 'examples', ignore=shutil.ignore_patterns('*.png', '*.svg'))
    fonts = Path(args.fritzing_app) / 'resources/fonts'
    css = []
    for family, file, weight in FONTS:
        target = out / 'fonts' / Path(file).name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(fonts / file, target)
        css.append(f"@font-face {{ font-family: '{family}'; src: url('fonts/{target.name}'); font-weight: {weight}; }}")
    (out / 'fonts.css').write_text('\n'.join(css) + '\n')
    idx, skipped = index(args.cli)
    (out / 'parts-index.json').write_text(json.dumps(idx, separators=(',', ':'), ensure_ascii=False))
    print(f"{len(idx['parts'])} parts indexed ({skipped} without a known source), {len(idx['archives'])} archives -> {out}")


if __name__ == '__main__':
    main()
