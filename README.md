# Rich Text to Post Script filters

There are two variations of the filter. The `rt2ps` filter converts 7-bit ASCII character strings formatted in accordance
 with RFC 1341.
The `et2ps` filter converts character strings in accordance with RFC 1563, 
which is a refinement of RFC 1341.

The filters support various formatting and pagination commands including 
*FlushBoth* which causes the affected text to be filled and padded so
           as to create smooth left and right margins, i.e., to be
           fully justified.
To accomplish this, most of the work is done with PostScript itself.
The filters generate a PostScript program which measures the width of 
characters in a line and dynamically adjusts the width of inter-word spacing
to achieve full justification.
