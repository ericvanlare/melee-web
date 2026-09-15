#!/usr/bin/env python3
"""Compare two finalized original sessions without modifying either bundle."""
import argparse,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from reference_session_comparison import compare_bundles

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('original',type=Path);p.add_argument('replay',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();report=compare_bundles(a.original,a.replay)
    # Derived reports never overwrite an existing result or enter the raw bundle.
    for source in (a.original,a.replay):
        if a.output.resolve().is_relative_to(source.resolve()):raise SystemExit('Reports must be outside raw bundles')
    with a.output.open('x') as f:json.dump(report,f,indent=2,sort_keys=True);f.write('\n')
    print(report['status']);return {'matched':0,'diverged':1}.get(report['status'],2)
if __name__=='__main__':raise SystemExit(main())
