
CC = gcc
CFLAGS = -O

all : prolog.h paginate.ps rt2ps et2ps

clean :
	rm -f rt2ps et2ps *.o *.bak junk *~ prolog.h paginate.ps

#----------------------------------------------------------------------------
# paginate.ps.verbose is the PostScript source code for the et2ps and rt2ps
# filters. When it is modified, the pstrip command "strips" it, to make it
# much smaller and (almost) impossible for a human to read. Then, the mkincl
# command turns it into a static data structure in an include file, prolog.h,
# which is compiled into rt2ps and et2ps.
#

prolog.h : paginate.ps	
	./mkincl < paginate.ps > $@

paginate.ps : paginate.ps.verbose
	./pstrip < paginate.ps.verbose > $@

#----------------------------------------------------------------------------
# et2ps and rt2ps
#

rt2ps : rt2ps.c
	$(CC) $(CFLAGS) $? -o $@

et2ps : et2ps.c
	$(CC) $(CFLAGS) $? -o $@
