#!/usr/bin/env python3
"""Fetch only the reviewed official SDK. Never install tools or modify an existing checkout."""
import json, subprocess,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[1]
lock=json.loads((root/'third_party/dependency-lock.json').read_text())['sdk']
sdk=root/'third_party/vst3sdk'
def run(*args): subprocess.run(args,check=True)
def sha(path): return subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'],text=True).strip()
new_checkout = not sdk.exists()
if not new_checkout:
    if not (sdk/'.git').exists(): raise SystemExit('Existing SDK is a source copy; verify it explicitly or use a fresh checkout. Refusing overwrite.')
else: run('git','clone','--no-checkout',lock['repository'],str(sdk))
if new_checkout or sha(sdk)!=lock['commit']:
    dirty=subprocess.check_output(['git','-C',str(sdk),'status','--porcelain'],text=True)
    # --no-checkout leaves the initial index empty. That is not a user's edit.
    if dirty and not new_checkout: raise SystemExit('SDK contains local changes; refusing checkout')
    run('git','-C',str(sdk),'checkout','--detach',lock['commit'])
run('git','-C',str(sdk),'submodule','update','--init',*lock['submodules'].keys())
for name,expected in lock['submodules'].items():
    if sha(sdk/name)!=expected: raise SystemExit('Submodule mismatch: '+name)
print('Pinned VST3 SDK verified:',lock['commit'])
