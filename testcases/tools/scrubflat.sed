#!/bin/sh
sed '
/^>30$/ {
# found date-type attribute - read in next line
    N
# replace date on the second line
    s/".*"$/"Fri Jan 01 00:00:00 2010"/
}
/^>218$/ {
# found date-type attribute - read in next line
    N
# replace date on the second line
    s/".*"$/"Fri Jan 01 00:00:00 2010"/
}
/^>219$/ {
# found date-type attribute - read in next line
    N
# replace date on the second line
    s/".*"$/"Fri Jan 01 00:00:00 2010"/
}
/^>5$/ {
# found password attribute - read in next line
    N
# replace date on the second line
    s/".*"$/"$SHA1$HvbGhcV17U9y$KwSMjZCZZW7tW9HbR6HwZ7ovM\/8="/
}
/^>224$/ {
# found CONNINFO attribute - read in next line
    N
# replace date on the second line
    s/".*"$/"0 0 0 0 1262304000"/
}
/^>84$/ {
# found LAST attribute - read in next line
    N
# replace date on the second line
    s/".*"$/"#1;127.0.0.1;Fri Jan 01 00:00:00 2010;;;;;;;0;0;;;;;;;"/
}'
