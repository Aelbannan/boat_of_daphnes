#!/bin/bash
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
export ROOT
# usage: run_mates.sh <room> <record s> <total s> <set>   (sets: A B C L S W V; Q 16 distinct custom players; Z 16 stock)
# BOD_MATE_COUNT=<n> limits Q and Z to the first n players.
# BOD_MATE_SERVER=ws://host:port picks the server (default: the local one on 8811)
room=$1; rec=${2:-14}; tot=${3:-200}; set=${4:-A}; srv=${BOD_MATE_SERVER:-ws://127.0.0.1:8811}
export MATE_GEAR=1
M=${BOD_MATE_MODS:-$ROOT/build/mate_mods}
command -v cygpath > /dev/null && M=$(cygpath -m "$M") # node takes Windows paths (and the shell would convert only the first of a list)
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
E) start Mate 150 0 "$M/samus-ww/Samus" ;; # the anchor_puppets scenario's Mate (a Samus pack)
Q|QH) # 16 players with 16 different bodies, equipment sets and voice banks (the extracted test mods in build/mate_mods: the
   # folders of models_9f5d1 and the single-model mods; a voice bank is the mod's own, or for a mod without sound a copy of
   # another's with one sample byte changed, so that it is a bank of its own (build/mate_mods/voicevar_*)).
   P=$M/models_9f5d1/models
   i=0
   for spec in "$M/457_young_link/models/Young Link" "$M/samus-ww/Samus" "$M/vaati_1_0_1/Vaati" "$M/malon_romani_85101/Malon & Romani"                "$M/dslink_1_1_0/DSLink" "$M/linka" "$M/linkplush/LinkPlush" "$P/Aryll" "$P/Fado" "$P/Fox Link" "$P/Goku" "$P/LinkCheer"                "$P/LinkZora" "$P/Lucario" "$P/Medli" "$P/PoorMila"; do
     name=$(basename "$spec" | tr -d ' &')
     v="$spec"
     [ -f "$spec/sound/JaiInit.aaf" ] || v="$M/voicevar_$i"
     [ -d "$v" ] || v=""
     [ $i -ge ${BOD_MATE_COUNT:-16} ] && break
     col=$((i % 4)); row=$((i / 4))
     dz=$(( -130 - row * 100 ))
     [ "$set" = QH ] && [ $i -ge 8 ] && dz=$(( 1500 + row * 100 )) # the second half behind the camera
     start "$name" $(( (col - 2) * 100 + 50 )) $dz "link=$spec,equip=$spec${v:+,voice=$v}"
     i=$((i + 1))
   done ;;
Z|ZH) for i in $(seq 0 $(( ${BOD_MATE_COUNT:-16} - 1 ))); do
     col=$((i % 4)); row=$((i / 4))
     dz=$(( -130 - row * 100 ))
     [ "$set" = ZH ] && [ $i -ge 8 ] && dz=$(( 1500 + row * 100 )) # the second half behind the camera
     start "Stock$i" $(( (col - 2) * 100 + 50 )) $dz ""
   done ;;
esac
