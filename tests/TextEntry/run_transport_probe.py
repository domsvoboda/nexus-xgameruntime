#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Account-free GDK/PE bridge/loopback test; replaces GTK with a synthetic peer."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runtime', type=Path, required=True)
parser.add_argument('--stage', type=Path, required=True)
parser.add_argument('--probe', type=Path, required=True)
parser.add_argument('--arch', choices=('x86_64', 'i386'), default='x86_64')
args = parser.parse_args()
module = args.stage.resolve() / 'lib/wine' / (args.arch + '-windows')
files = ['xgameruntime.dll', 'nexus-text-entry.py', 'nexus-text-entry-host.exe']
for name in files:
    if not (module / name).is_file():
        parser.error('Missing staged component: ' + name)
root = Path(tempfile.mkdtemp(prefix='nexus text entry transport '))
runtime = root / 'runtime'
# Reflink where available; never patch the supplied runtime or a real prefix.
subprocess.run(['cp', '-a', '--reflink=auto', str(args.runtime.resolve()), str(runtime)], check=True)
destination = runtime / 'files/lib/wine' / (args.arch + '-windows')
for name in files:
    target = destination / name
    if target.exists():
        target.chmod(0o700)
    shutil.copy2(module / name, target)
prefix = root / 'private prefix with spaces'
env = dict(os.environ)
for key in list(env):
    if key.startswith(('XODUS_', 'XUSER_', 'NEXUS_', 'STEAM_COMPAT_')) or key in ('WINESERVER', 'WINELOADER', 'WINEDLLPATH', 'WINEARCH'):
        env.pop(key, None)
env.update(WINEPREFIX=str(prefix), WINEDEBUG='-all',
           WINEDLLOVERRIDES='mscoree,mshtml,winemenubuilder.exe=d;xgameruntime=n,b',
           LD_LIBRARY_PATH=':'.join(str(runtime / 'files/lib' / arch) for arch in ('x86_64-linux-gnu', 'i386-linux-gnu')))
wine = runtime / 'files/bin/wine'
log = root / 'transport.log'
status = 1
try:
    with log.open('w') as output:
        subprocess.run([str(wine), 'wineboot', '-u'], env=env, stdout=output, stderr=subprocess.STDOUT, timeout=60, check=True)
    system = prefix / 'drive_c/windows' / ('system32' if args.arch == 'x86_64' else 'syswow64')
    shutil.copy2(module / 'nexus-text-entry-host.exe', system / 'nexus-text-entry-host.exe')
    shutil.copy2(module / 'nexus-text-entry.py', system / 'text-entry-model.py')
    shutil.copy2(Path(__file__).with_name('transport_fixture.py'), system / 'nexus-text-entry.py')
    with log.open('a') as output:
        result = subprocess.run([str(wine), str(args.probe.resolve())], env=env, stdout=output,
                                stderr=subprocess.STDOUT, timeout=90)
    text = log.read_text(errors='replace')
    passed = result.returncode == 0 and text.count('PASS native scenario ') == 11 and 'ALL NATIVE TESTS PASSED' in text
    status = 0 if passed else 1
    proof = {'passed': passed, 'architecture': args.arch, 'scenarios': 11 if passed else 0,
             'scope': 'GDK async/result, private-token PE bridge and loopback; synthetic UI',
             'gtkDisplayTested': False, 'accountsUsed': False,
             'dllSha256': hashlib.sha256((module / 'xgameruntime.dll').read_bytes()).hexdigest(),
             'bridgeSha256': hashlib.sha256((module / 'nexus-text-entry-host.exe').read_bytes()).hexdigest()}
    (root / 'results.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps(proof))
    if not passed:
        print(text[-2400:])
finally:
    subprocess.run([str(runtime / 'files/bin/wineserver'), '-k'], env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
    if status == 0:
        shutil.rmtree(root)
    else:
        print('Failure evidence retained in', root)
raise SystemExit(status)
