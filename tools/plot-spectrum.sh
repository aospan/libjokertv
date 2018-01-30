#!/bin/bash
# Joker TV blind scan spectrum draw using gnuplot
# (c) Abylay Ospan <aospan@jokersys.com>, 2018
# LICENSE: GPLv2

usage() { echo "Usage: $0 -p power.csv -l locked.csv -o out.png" 1>&2; exit 1; }

while getopts "o:l:p:" o; do
    case "${o}" in
        o)
            out=${OPTARG}
            ;;
        p)
            power=${OPTARG}
            ;;
        l)
            locked=${OPTARG}
            ;;
        *)
            usage
            ;;
    esac
done
shift $((OPTIND-1))

if [ -z "${out}" ] || [ -z "${power}" ] || [ -z "${locked}" ]; then
    usage
fi

#get LOCKed transponders
#format example: "11889","13v V(R)","DVB-S2","7198"
while IFS=, read -r freq pol standard ksym
do
    freq=${freq//\"}
    standard=${standard//\"}
    ksym=${ksym//\"}
    pol=${pol//\"}
    if [ "$freq" -eq "$freq" ] 2>/dev/null; then
        labels+="set arrow from $freq, graph 0 to $freq, graph 1 nohead"
        labels+=$'\n'
        labels+="set label \"$standard $freq $pol $ksym\" at $freq-5,graph 0 rotate"
        labels+=$'\n'
    fi
done < $locked

#draw final spectrum
gnuplot -persist <<-EOFMarker
    set grid xtics
    set xtics 40 format "%.1f" scale 2 rotate
    set grid x2tics
    set autoscale fix
    set x2tics 10 format "%.1f" scale 1 offset 0,graph 0 rotate font "sans,6"
    $labels
    set terminal png size 1600,1200
    set output '$out'
    plot '$power' with lines
EOFMarker
