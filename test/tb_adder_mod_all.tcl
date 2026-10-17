# Add every signal in the dump to the wave view
set nfacs [gtkwave::getNumFacs]
set all {}
for {set i 0} {$i < $nfacs} {incr i} { lappend all [gtkwave::getFacName $i] }
gtkwave::addSignalsFromList $all
gtkwave::/Time/Zoom/Zoom_Full
