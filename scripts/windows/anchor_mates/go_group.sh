#!/bin/bash
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
export ROOT
# usage: go_group.sh <scenario> <tag> <room> <set> [extra scenario.py args]   mates, then the scenario; log build/shots/sc_<tag>.log
sc=$1; tag=$2; room=$3; set=$4; shift 4
cd $ROOT/scripts/windows/anchor_mates
L=$ROOT/build/shots; mkdir -p $L
$PWD/run_mates.sh $room 14 420 $set
./run_sc.sh $sc $tag --cvar gNetwork.Anchor.Room=$room "$@" > $L/sc_$tag.log 2>&1
echo done >> $L/sc_$tag.log
# only the mates of my own server (port 8811)
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='node.exe'\" | Where-Object { \$_.CommandLine -match 'pose_replay' -and \$_.CommandLine -match '8811' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }"
