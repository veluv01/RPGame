#!/usr/bin/env python3
"""Run upstream game logic tests and short simulator smoke runs."""
import argparse
from pathlib import Path
import subprocess
import sys
import json
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import gamecfg
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--logic',action='store_true')
p.add_argument('--smoke',action='store_true')
p.add_argument('--quick',action='store_true')
p.add_argument('names',nargs='*')
a=p.parse_args()
names=a.names or sorted(d.name for d in (ROOT/'games').iterdir() if d.is_dir())
out=ROOT/'docs/game-tests';out.mkdir(parents=True,exist_ok=True)
result_path=out/(('logic' if a.logic else 'smoke')+'-results.json')
results=json.loads(result_path.read_text()) if result_path.exists() else []
for name in names:
    row={'game':name}
    for mode in ('logic','smoke'):
        if not getattr(a,mode):continue
        cmd=[sys.executable,str(ROOT/'tools/hosttests.py'),name]+(gamecfg.load(ROOT/'games'/name).QUICK_ARGS if a.quick else []) if mode=='logic' else [sys.executable,str(ROOT/'tools/chsim/chsim.py'),'run',name,'--frames','90','--max-seconds','12','--input','20:A,22:,45:DOWN,47:,65:B,67:']
        print(f'{name}: {mode}',flush=True)
        r=subprocess.run(cmd,capture_output=True,text=True)
        (out/f'{name}.{mode}.log').write_text(r.stdout+r.stderr)
        row[mode]='pass' if r.returncode==0 else 'fail'
        print(row[mode],flush=True)
    results=[r for r in results if r["game"]!=name]+[row]
    result_path.write_text(json.dumps(results,indent=2)+'\n')
sys.exit(any('fail' in row.values() for row in results))
