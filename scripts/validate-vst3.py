#!/usr/bin/env python3
import json,subprocess,re,sys
from pathlib import Path
build=Path(sys.argv[1] if len(sys.argv)>1 else 'build/mac-arm64-clt').resolve()
validator=build/'bin/Release'/('validator.exe' if sys.platform=='win32' else 'validator')
results=[]
root=Path(__file__).resolve().parents[1]
identities=json.loads((root/'common/vst3/plugin-identities.json').read_text())
for identity in identities:
    bundle=build/'VST3/Release'/('Just_'+identity['slug']+'.vst3')
    run=subprocess.run([str(validator),str(bundle)],capture_output=True,text=True,timeout=60)
    text=run.stdout+run.stderr
    (build/(bundle.stem+'-validator.log')).write_text(text)
    counts=re.search(r'Result:\s*(\d+) tests passed,\s*(\d+) tests failed',text)
    result={'target':bundle.stem,'exit':run.returncode,'passed':int(counts[1]) if counts else None,'failed':int(counts[2]) if counts else None}
    results.append(result);print(json.dumps(result),flush=True)
(build/'validator-summary.json').write_text(json.dumps(results,indent=2)+'\n')
if len(results)!=10 or any(x['exit'] or x['failed']!=0 for x in results):raise SystemExit(1)
