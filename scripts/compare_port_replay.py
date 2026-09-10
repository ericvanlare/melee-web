#!/usr/bin/env python3
"""Compare a complete port probe with two independently repeated retail captures."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from port_replay_validation import compare_paths

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('retail_a',type=Path)
    parser.add_argument('retail_b',type=Path)
    parser.add_argument('port',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if args.output and args.output.resolve() in {x.resolve() for x in (args.retail_a,args.retail_b,args.port)}:
        parser.error('output cannot overwrite an input capture')
    report=compare_paths(args.retail_a,args.retail_b,args.port)
    rendered=json.dumps(report,indent=2,sort_keys=True)+'\n'
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(rendered)
    print(rendered,end='')
    return {'declared_state_match':0,'diverged':1}.get(report['status'],2)
if __name__=='__main__': raise SystemExit(main())
