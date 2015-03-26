set angles degrees
set polar
set size square
unset border

set grid polar 45
#set grid ls 10 lt 1 lc 0 lw 0.3

#unset param
set title "Telescope position (t,d)"
#set xlabel "d-axis"
#set ylabel "t-axis"

# actually [90:-90] but can't seem to make that happen so plot transforms d
set rrange [-90:90]
#set trange [*:*]
#set dummy t

set view equal xy

set_label(x, text) = sprintf("set label '%s' at (200*cos(%f)),(200*sin(%f)) center", text, x, x)
eval set_label(0,"0")
eval set_label(45,"45")
#eval set_label(90,"90")
eval set_label(135,"135")
eval set_label(180,"180")
eval set_label(225,"-135")
eval set_label(270,"-90")
eval set_label(315,"-45")

# display xtics on axis instead of border
unset xtics
unset ytics

#set logscale r 10
set rtics ("90" -90, "45" -45, "0" 0, "-45" 45, "-90" 90)
set rtics axis scale 0.5,0 nomirror rotate by -270 offset 0, 0.7
#set xlabel "t"
#set ylabel "d"

set key lmargin

set style line 1 lc rgb '#0060ad' pt 7 # circle
set style line 2 lc rgb '#0060ad' pt 5 # square

plot '-' using 1:((-1*$2)) w p ls 2 notitle
-30 0
e

pause 10
