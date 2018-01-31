#!/bin/bash
# Joker TV blind scan spectrum draw using gnuplot
# (c) Abylay Ospan <aospan@jokersys.com>, 2018
# LICENSE: GPLv2

usage() { echo "Generate spectrum based on Joker TV blind scan results
        Usage:
            $0 -p power.csv -l locked.csv -o out.png -y voltage
                -p file produced by joker-tv --blind-power
                -l file produced by joker-tv --blind-out
                -o output PNG filename with spectrum
                -y voltage. 13 or 18
    " 1>&2; exit 1; }

while getopts "y:o:l:p:" o; do
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
        y)
            voltage=${OPTARG}
            ;;
        *)
            usage
            ;;
    esac
done
shift $((OPTIND-1))

if [ -z "${out}" ] || [ -z "${power}" ] || [ -z "${locked}" ] || [ -z "${voltage}" ]; then
    usage
fi

if [ "${voltage}" = "13" ]; then
    colour="red"
else
    colour="blue"
fi

#get min freq from power list
minfreq=0
while IFS=" " read -r freq val
do
    freq=${freq//\"}
    if [ "$freq" -lt "$minfreq" ] || [[ "$minfreq" -eq "0" ]] ; then
        minfreq=$freq
    fi
done < $power

#get LOCKed transponders
#format example: "11889","13v V(R)","DVB-S2","7198"
while IFS=, read -r freq pol standard ksym other
do
    freq=${freq//\"}
    standard=${standard//\"}
    ksym=${ksym//\"}
    pol=${pol//\"}
    other=${other//\"}
    if [ "$freq" -eq "$freq" ] 2>/dev/null && [[ "$freq" -ge "$minfreq" ]] && [[ "$pol" =~ "${voltage}".+ ]]; then
        if [ "${voltage}" = "13" ]; then
            pol_short="V"
        else
            pol_short="H"
        fi
        labels+="set arrow from $freq, graph 0 to $freq, graph 1 nohead ls 50"
        labels+=$'\n'
        labels+="set label \"$standard $freq$pol_short $ksym $other\" at $freq-5,graph 0 rotate textcolor rgb \"black\""
        labels+=$'\n'
    fi
done < $locked

title=`date -R`

#draw final spectrum
echo "Generating spectrum ..."
gnuplot -persist <<-EOFMarker
    set title "Built with Joker TV on $title \n\
        https://tv.jokersys.com"
    set style line 50 lt 1 lc rgb "black" lw 1
    set grid xtics
    set xtics 40 format "%.1f" scale 2 rotate
    set grid x2tics
    set autoscale fix
    set x2tics 10 format "%.1f" scale 1 offset 0,graph 0 rotate font "sans,6"
    $labels
    set terminal png size 1600,1200
    set output '$out'
    plot '$power' lt rgb "$colour" with lines
EOFMarker
