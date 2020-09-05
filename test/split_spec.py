#!/usr/bin/env python3

# This script uses the spec_tests.py file to conver the .txt specifications into JSON data.
# 
# The data can be used to better debug the unterlying parser by running the parser against specific input
# 
# usage: 
#   $ cd tests
#   $ ./split_spec.py
#
#   output defaults to a subfolder called 'tests'
#   each .txt file will produce a separate subfolder whose name matches the name of the file
#   for each example in the .txt file a separate .json file is created. The examples are simply numbered 

import sys
from pathlib import Path
from spec_tests import get_tests
import argparse
import json

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='extract tests from specs to json.')
    parser.add_argument('--input-dir', dest='dirs', nargs='*', default=['.'], help='directory containing the specs files')
    parser.add_argument('--output-dir', dest='out', nargs='?', default='tests', help='directory where the json files are written')
    args = parser.parse_args(sys.argv[1:])
    baseOutDirectory = Path(args.out)
    if (not baseOutDirectory.exists()):
        baseOutDirectory.mkdir()
    
    for directory in args.dirs:
        print(f"entering directory: {directory}")
        p = Path(directory)
        for f in p.iterdir():
             if (f.suffix == ".txt"):
                 print(f"\tprocessing file: {f.name}")
                 tests = get_tests(str(f))
                 outDir = baseOutDirectory / f.stem
                 if (not outDir.exists()):
                     outDir.mkdir()
                 testNum = 1
                 for test in tests:
                     jsonData = json.dumps(test)
                     outFile = outDir / f"{testNum}.json"
                     testNum = testNum + 1
                     with open(outFile,"w") as ws:
                         ws.write(jsonData)
                    