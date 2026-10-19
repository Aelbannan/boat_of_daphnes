#!/bin/bash
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
export ROOT
# usage: run_mates.sh <room> <record s> <total s> <set>   (sets: A B C L S W V)
room=$1; rec=${2:-14}; tot=${3:-200}; set=${4:-A}; srv=ws://127.0.0.1:8811
export MATE_GEAR=1
M=${BOD_MATE_MODS:-$ROOT/build/mate_mods}
cd $ROOT/server/anchor
L=$ROOT/build/shots
start() { # name dx dz spec [legacy]
  MATE_LEGACY=$5 MATE_NAME="$1" MATE_DX=$2 MATE_DZ=$3 nohup node test/pose_replay.mjs $srv $room $rec $tot "$4" > $L/mates_${room}_$1.log 2>&1 &
  echo $! >> $L/mate_pids.txt
}
case $set in
A) start YoungOnly -110 90 "link=$M/Young"
   start VaatiEquip 110 90 "equip=$M/Vaati"
   start Vanilla 0 200 ""
   start GokuVoice 0 290 "voice=$M/Goku" ;;
B) start SamusAll 0 110 "$M/Samus"
   start Vanilla -100 200 ""
   start AryllVoice 100 200 "voice=$M/Aryll" ;;
C) start Mixed 0 110 "link=$M/Young,equip=$M/Samus,voice=$M/Aryll"
   start VaatiEquip -110 200 "equip=$M/Vaati"
   start Vanilla 110 200 "" ;;
L) start OlderClient -110 100 "link=$M/Young" 1
   start Vanilla 110 100 "" ;;
S) start Vanilla 60 40 "" ;;
V) start YoungVoice -90 60 "voice=$M/Young"
   start SamusVoice 90 60 "voice=$M/Samus"
   start VaatiVoice 0 150 "voice=$M/Vaati" ;;
1) start VaatiVoice -60 60 "voice=$M/Vaati" ;;
M) start SamusAll 0 110 "$M/Samus"
   start Mixed -90 160 "link=$M/Young,equip=$M/Samus,voice=$M/Aryll"
   start YoungOnly 90 160 "link=$M/Young"
   start VaatiAll -150 60 "$M/Vaati"
   start GokuAll 150 60 "$M/Goku"
   start Vanilla 0 220 "" ;;
W) start YoungOnly -110 90 "link=$M/Young"
   start VaatiEquip 110 90 "equip=$M/Vaati"
   start Vanilla 0 200 "" ;;
esac
