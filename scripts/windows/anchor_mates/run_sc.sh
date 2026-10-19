#!/bin/bash
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
export ROOT
# usage: run_sc.sh <scenario> <tag> [extra scenario.py args]
cd $ROOT
sc=$1; tag=$2; shift 2
python scripts/windows/scenario.py $sc --exe $ROOT/build/windows/app/boat-of-daphnes.exe --module ${BOD_MODULE:?set BOD_MODULE to the game module} --tag $tag "$@"
