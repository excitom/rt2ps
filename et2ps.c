/**
 * Name: et2ps
 *
 * Function: convert MIME Enriched Text to PostScript
 *
 * Author: Tom Lang
 *
 * Date: 07/05/94
 *
 * Data Format: 7-bit ASCII character strings formatted in accordance
 *	with RFC 1563.
 */
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "prolog.h"

/* number of keywords */
#define MAXKEY 15

/* length of longest keyword (RFC says allow 60 chars plus <,/, and > ) */
#define MAXKEYLEN 63

/*
 * this array represents all keywords recognized by this program,
 * but not necessarily all legal keywords. 
 *
 * the array is sorted by my perception of frequency of usage, in
 * an attempt to reduce search time.
 */
#define K_BOLD		0
#define K_ITALIC	1
#define K_CENTER	2
#define K_FIXED		3
#define K_UNDERLINE	4
#define K_INDENT	5
#define K_INDENTR	6
#define K_BIGGER	7
#define K_SMALLER	8
#define K_FL		9
#define K_FR		10
#define K_FB		11
#define K_NOFILL	12
#define K_PARAM		13
#define K_EXCERPT	14
/*
 * the "newline" keyword is not a <> delimited token
 */
#define K_NL		15

static char *keys[MAXKEY] = { 
	"bold>",		/* 0 */
	"italic>",		/* 1 */
	"center>",		/* 2 */
	"fixed>",		/* 3 */
	"underline>",		/* 4 */
	"indent>",		/* 5 */
	"indentright>",		/* 6 */
	"bigger>",		/* 7 */
	"smaller>",		/* 8 */
	"flushleft>",		/* 9 */
	"flushright>",		/* 10 */
	"flushboth>",		/* 11 */
	"nofill>",		/* 12 */
	"param>",		/* 13 */
	"excerpt>"		/* 14 */
};

/*
 * tokens are built up in "buff"
 * PostScript code and macros are built in code
 */
static char buff[1024];
static char code[1024];

/*
 * place to save directory name from which program is launched
 */
static char dir[255];

/*
 * font attribute masks, designed so that they can OR together.
 * note that font change from default (Helvetica) to fixed (Courier) is
 * handled as an attribute change rather than a font change. this limits
 * us to two fonts, but that's all that's required by MIME.
 */
#define BOLD 1
#define ITALIC 2
#define FIXED 4

/*
 * table of font names, indexed by attribute mask, which is
 * an OR of bold, italic, and fixed font.
 *
 * when the prolog code is written to standard output, macros defining
 * short-hand names for the fonts are defined.
 */
static char *font[8] = {
	"f1",
	"f1b",
	"f1i",
	"f1bi",
	"f2",
	"f2b",
	"f2i",
	"f2bi"
	};

/*
 * justifcation flags are used as bit flags, to allow nesting.
 *
 * left justifcation implies no special processing of the output. centering,
 * full justification, and right justification require extra work.
 *
 * the most recent (innermost) command takes precedence.
 *
 * if justification is turned on or off in the middle of
 * a line, a line break is assumed before and after the formatting change.

 * these values correspond to the parameters for the
 * PostScript JU macro. JU 0 = left, JU 1 = center, JU 2 = right, JU 3 = full
 */
#define L_JUST 0
#define CENTER 1
#define R_JUST 2
#define F_JUST 3

/*
 */
/*
 *  PostScript page coordinates
 *    measured in "points," where 72 points = 1 inch
 *    lower left page corner = 0,0
 *    8.5" x 11" paper, portrait orientation assumed
 *    indentation unit is .5 inch, i.e. 36 points
 */
#define Y_TOP 720
#define Y_BOT 72
#define X_LEFT 72
#define X_RIGHT 540
#define NORMAL_FONT_SIZE 10
#define SMALL_FONT_SIZE 6
#define LINE_HEIGHT 12
#define INDENT 36

/*
 * global static variables
 */
struct {
  char *n;		/* pointer to program name */
  char *d;		/* pointer to program directory name */
  int keyword;		/* flag: keyword just processed */
  int c;		/* index into the token buffer */
  int space;		/* flag: collecting white space. this is done to
			   optimize the PostScript code - doing one command
			   for multiple spaces rather than one each space */
  int altFont;		/* flag: use alternate font, e.g. Times vs. Helvetica*/
  int justify;		/* mask: justification attributes */
  int jstack;		/* justification stack index */
  int nl;		/* flag: <nl> processed for this line */
  int fs;		/* current font size */
  int pfs;		/* previous font size */
  int ffs;		/* full font size */
  int atMargin;		/* flag: at left margin now */
  int prolog;		/* flag: pre-pend PostScript prolog to output */
  int suppress;		/* flag: output suppressed (within a param) */
  int underline;	/* flag: underline attribute turned on/off */
  int mask;		/* font mask: used to select from font array */
  int pm;		/* previous font mask */
  int box;		/* flag: draw box around each page */
  int showTags;		/* flag: show unrecognized MIME tags */
  int hdr;		/* flag: print running headers */
  int pg;		/* running header page number */
} g = {
  NULL,			/* n */
  &dir[0],		/* d */
  0,			/* keyword */
  0,			/* c */
  0,			/* space */
  0,			/* altFont */
  0,			/* justify */
  0,			/* jstack */
  0,			/* nl */
  NORMAL_FONT_SIZE,	/* fs */
  0,			/* pfs */
  NORMAL_FONT_SIZE,	/* ffs */
  1,			/* atMargin */
  1,			/* prolog */
  0,			/* suppress */
  0,			/* underline */
  0,			/* mask */
  -1,			/* pm */
  0,			/* box */
  0,			/* showTags */
  0,			/* hdr */
  1			/* pg */
};

/*
 * justification attribute stack
 */
#define MAXJSTACK	16
static int jstack[MAXJSTACK];

/*
 * function prototypes
 */
void prolog();
void epilog();
void tokenOutput( char * );
void tab();
int  keywordMatch( char * );
void controlOutput( int );
void newline();
void pushJustify( int );
void popJustify( int );
void toggleFont( int );
void foldLow( char * );
int  getArgs( int, char ** );
char *baseName( char *, char * );
char *dirName( char *, char * );
void showHelp();

int
main(int argc, char **argv)
{
	int c;			/* input stream character */
	int key;		/* keyword index */

	/*
	 * process command line arguments, bail out if there's a problem
	 */
	if (getArgs( argc, argv ) != 0)
		exit(1);

	/*
	 * output PostScript prolog code
	 */
	prolog();

	/*
	 * read data stream from standard input and filter to stdout
	 */
	while((c = getchar()) != EOF) {
		switch ((char) c) {
		/*
		 * "newline" in the input stream.
		 * An isolated newline is treated as a space.
		 * N consecutive newlines are treated as N-1 line breaks.
		 */
		case '\n' :
			/*
			 * if at the left margin, we're in a newline
			 * sequence.
			 */
			if(g.atMargin) {
				controlOutput(K_NL+1);
			}
			else {
				c = getchar();
				if ((char) c == '\n') {
					tokenOutput(buff);
					controlOutput(K_NL+1);
				}
				else {
					ungetc(c, stdin);
					if(g.space == 0) {
						tokenOutput(buff);
						g.space = 1;
					}
					buff[g.c++] = ' ';
				}
			}
			break;
		/*
		 * tab character
		 */
		case '\t' :
			tokenOutput(buff);
			tab();
			break;
		/*
		 * carriage returns are ignored
		 */
		case '\r' :
			break;
		/*
		 * space character
		 */
		case ' ' :
			if(g.space == 0) {
				tokenOutput(buff);
				g.space = 1;
			}
			buff[g.c++] = (char) c;
			break;
		/*
		 * two consecutive <'s are interpreted as a single,
		 * literal '<'. else, this is the beginning of a keyword.
		 */
		case '<' :
			if(g.space)
				tokenOutput(buff);
			c = getchar();
			if ((char) c == '<') {
				buff[g.c++] = '<';
			}
			else {
				ungetc(c, stdin);
				tokenOutput(buff);
				buff[g.c++] = (char) c;
				g.keyword = 1;
			}
			break;
		case '>':
			if(g.space)
				tokenOutput(buff);
			buff[g.c++] = (char) c;
			if(g.keyword) {
				key = keywordMatch(buff);
				if(key == 0) {
					if(g.showTags)
						tokenOutput(buff);
					else {
						g.c = 0;
						g.space = 0;
						g.keyword = 0;
						g.atMargin = 0;
					}
				}
				else
					controlOutput(key);
			}
			break;
		/*
		 * characters which are special to the PostScript interpreter
		 */
		case '\\' :
		case '(' :
		case ')' :
			if(g.space)
				tokenOutput(buff);
			buff[g.c++] = '\\';
			buff[g.c++] = (char) c;
			if(g.keyword == 0)
				g.atMargin = 0;
			break;
		default:
			/*
			 * guard against extraneous stuff in the input
			 */
#if 0
			if ( iscntrl( (char) c) ) {
				c = (int) '.';
			}
#endif
			/*
			 * copy character to the buffer
			 */
			if(g.space)
				tokenOutput(buff);
			buff[g.c++] = (char) c;
			if(g.keyword == 0)
				g.atMargin = 0;
		}
	}
	/*
	 * wrap up the PostScript output
	 */
	epilog();
	exit (0);
}
/*
 * output a 4-tuple token of the form:
 * [ (string) size font action ] C
 *
 * where:
 *	(string) is a character string to be output
 *	size is the font's point size, or 0 for no change
 *	font is the name of the font, or "x" for no change
 *	action is the action code for this token:
 *	0 = show character string
 *	1 = show underlined character string
 *	2 = show space(s)
 *	3 = show underlined space(s)
 *	4 = tab
 *	5 = underlined tab
 *	6 = show subscript			(unused, from RFC 1341)
 *	7 = show underlined subscript		(unused, from RFC 1341)
 *	8 = show superscript			(unused, from RFC 1341)
 *	9 = show underlined superscript		(unused, from RFC 1341)
 *	the trailing "C" is a macro which causes the token to be processed.
 *
 *	There are shortcut macros for spaces and tabs:
 *	S = single space character
 *	US = single underlined space
 *	T = tab character
 *	UT = underlined tab character
 */
void
tokenOutput( char *b )
{
	int action;
	int fontSize;

	if(g.c == 0)
		return;
	if(g.suppress == 0) {
		if((g.space) && (g.c == 1)) {
			if(g.underline)
				printf("US\n");
			else
				printf("S\n");
		}
		else {
			buff[g.c] = 0;

			/*
			 * determine the "action code" for the "C" macro
			 */
			action = (g.space*2)+g.underline;

			/*
			 * there's no checking for too many nested <smaller>
			 * keywords, resulting in a 0 or negative font size.
			 * we'll leave the global variable alone so that
			 * corectly nested </smaller> keywords will eventually
			 * restore it. however, a font size smaller than 6
			 * will not be sent to the "C" macro.
			 */
			fontSize = (g.fs < 6) ? 6 : g.fs;
#ifdef DONTCARE
			if((g.fs == g.pfs) &&
			   (g.mask == g.pm))
				printf("[(%s) 0 x %i] C\n", buff, action);
			else
#endif
				printf("[(%s) %i %s %i] C\n", buff, g.fs, font[g.mask], action);
			g.pfs = g.fs;
			g.pm = g.mask;
		}
	}
	g.c = 0;
	g.space = 0;
	g.keyword = 0;
	g.atMargin = 0;
}
/*
 * output a tab
 */
void
tab()
{
	if(!g.suppress) {
		if(g.underline)
			printf("UT ");
		else
			printf("T ");
	}
}
/*
 * a character string delimited by < > has been found. see if it matches a
 * known keyword. this is a case-insensitive compare. zero is returned if
 * no match. a positive integer is returned if a match is found, equal to
 * the keyword code + 1. if the keyword is an "off" keyword, e.g. </bold>,
 * the return code is negated.
 *
 * for example, the keyword code for <bold> is 2. this routine will return
 * 3 if the keyword is <bold> or -3 if the keyword is </bold>.
 */
int
keywordMatch( char *buff )
{
	int i;
	int off = 0;
	int rc = 0;

	buff[g.c] = 0;
	foldLow(buff);
	if(*++buff == '/') {
		buff++;
		off = 1;
	}
	for( i=0; i<MAXKEY; i++ ) {
		if (strcmp(buff, keys[i]) == 0) {
			rc = (off) ? -(i+1) : (i+1);
			break;
		}
	}
	return(rc);
}
/*
 * a keyword has been matched in the input stream, so it's time to
 * process the associated operation. the key value + 1 is passed in.
 * if the key is negative, it represents turning an attribute off, e.g. /bold.
 * if it's positive, it represents turning an attribute on, e.g. italic.
 * some keywords don't have an off attribute, e.g. there's no /nl. if one of
 * these is encountered, the "off" is silently ignored.
 */
#define AttrOff (key < 0)
void
controlOutput( int key )
{
	int k = abs(key)-1;

	  switch(k) {
	  /* <nl> */
	  case K_NL:
		newline();
		break;
	  /* <bold> */
	  case K_BOLD:
		if AttrOff
			g.mask &= ~BOLD;
		else
			g.mask |= BOLD;
		break;
	  /* <italic */
	  case K_ITALIC:
		if AttrOff
			g.mask &= ~ITALIC;
		else
			g.mask |= ITALIC;
		break;
	  /* <fixed> */
	  case K_FIXED:
		if AttrOff
			g.mask &= ~FIXED;
		else
			g.mask |= FIXED;
		break;
	  /* <underline> */
	  case K_UNDERLINE:
		if AttrOff
			g.underline = 0;
		else
			g.underline = 1;
		break;
	  /* <center> */
	  case K_CENTER:
		if (g.atMargin == 0) {
			newline();
		}
		if AttrOff {
			popJustify(CENTER);
			printf("/JU %i def\n", g.justify);
		}
		else {
			pushJustify(CENTER);
			printf("/JU %i def\n", g.justify);
		}
		break;
	  /* <flushleft> */
	  case K_FL:
		if (g.atMargin == 0) {
			newline();
		}
		if AttrOff {
			popJustify(L_JUST);
			printf("/JU %i def\n", g.justify);
		}
		else {
			pushJustify(L_JUST);
			printf("/JU %i def\n", g.justify);
		}
		break;
	  /* <flushright> */
	  case K_FR:
		if (g.atMargin == 0) {
			newline();
		}
		if AttrOff {
			popJustify(R_JUST);
			printf("/JU %i def\n", g.justify);
		}
		else {
			pushJustify(R_JUST);
			printf("/JU %i def\n", g.justify);
		}
		break;
	  /* <flushboth> */
	  case K_FB:
		if (g.atMargin == 0) {
			newline();
		}
		if AttrOff {
			popJustify(F_JUST);
			printf("/JU %i def\n", g.justify);
		}
		else {
			pushJustify(F_JUST);
			printf("/JU %i def\n", g.justify);
		}
		break;
	  /* <nofill> */
	  case K_NOFILL:
		if (g.atMargin == 0) {
			newline();
		}
		if AttrOff {
			popJustify(L_JUST);
			printf("/JU %i def\n", g.justify);
		}
		else {
			pushJustify(L_JUST);
			printf("/JU %i def\n", g.justify);
		}
		break;
	  /* <indent> */
	  case K_INDENT:
		if AttrOff {
			if(g.atMargin)
				puts("DLM\n");
			else
				puts("DDLM\n");
		}
		else {
			if(g.atMargin)
				puts("ILM\n");
			else
				puts("DILM\n");
		}
		break;
	  /* <indentright> */
	  case K_INDENTR:
		if AttrOff {
			if(g.atMargin)
				puts("DRM\n");
			else
				puts("DDRM\n");
		}
		else {
			if(g.atMargin)
				puts("IRM\n");
			else
				puts("DIRM\n");
		}
		break;
	  /* <param> */
	  case K_PARAM:
		if AttrOff
			g.suppress = 0;
		else
			g.suppress = 1;
		break;
	  /* <bigger> */
	  case K_BIGGER:
		if AttrOff
			g.fs -=2;
		else
			g.fs +=2;
		g.ffs = g.fs;
		break;
	  /* <smaller> */
	  case K_SMALLER:
		if AttrOff
			g.fs +=2;
		else
			g.fs -=2;
		g.ffs = g.fs;
		break;
	  /* <excerpt> */
	  case K_EXCERPT:
		if (g.atMargin == 0) {
			newline();
		}
		if AttrOff {
			puts("DLM");
			toggleFont(0);
		}
		else {
			puts("ILM");
			toggleFont(1);
		}
		break;
	  default:
		fprintf(stderr, "INVALID KEYWORD\n");
  	}
	g.c = 0;
	g.keyword = 0;
}
/*
 * subroutine: process a line break.
 * 	this is called when 2 consecutive newline characters are found,
 *	or when justification mode is changed.
 */
void
newline()
{
	printf("NL\n");
	g.atMargin = 1;
}
/*
 * push justification attributes onto a stack. this allows nesting, for
 * example, of <flushleft>, <flushright>, <flushboth>, and <center>.
 */
void
pushJustify( int justify )
{
	jstack[g.jstack] = g.justify;
	if (++g.jstack >= MAXJSTACK) {
		fprintf(stderr, "Internal error, justify stack overflow\n");
		exit(1);
	}
	g.justify = justify;
}
/*
 * pop justification attributes off a stack. this allows nesting, for
 * example, of <flushleft>, <flushright>, <flushboth>, and <center>.
 */
void
popJustify( int justify )
{
	if (g.justify != justify) {
		fprintf(stderr, "Warning: Incorrect nesting of justification, output may be weird.\n");
	}
	if (--g.jstack < 0) {
		fprintf(stderr, "Internal error, justify stack underflow\n");
		exit(1);
	}
	g.justify = jstack[g.jstack];
}
/*
 * subroutine: toggle the main font between Helvetica and TimesRoman, based
 *	on the "alt" parameter. alt=true means set the alternate font, 
 *	alt=false means set the main font.
 *
 *	the default main font is Helvetica, and the alternate is TimesRoman.
 *	the polarity can be reversed by command line switch, which sets the
 *	main font to TimesRoman and the alternate font to Helvetica.
 */
void
toggleFont( int alt )
{
	char buff[256];

	if (alt ^ g.altFont) {
		strcpy(buff, "(Times-Roman) cvlit /f1 exch def ");
		strcat(buff, "(Times-Bold) cvlit /f1b exch def ");
		strcat(buff, "(Times-Italic) cvlit /f1i exch def ");
		strcat(buff, "(Times-BoldItalic) cvlit /f1bi exch def ");
	}
	else {
		strcpy(buff, "(Helvetica) cvlit /f1 exch def ");
		strcat(buff, "(Helvetica-Bold) cvlit /f1b exch def ");
		strcat(buff, "(Helvetica-Oblique) cvlit /f1i exch def ");
		strcat(buff, "(Helvetica-BoldOblique) cvlit /f1bi exch def ");
	}
	puts( buff );
}
/*
 * subroutine: fold alphabetic characters to upper case
 * note: VERY dependant on ASCII encoding
 */
void
foldLow( char *x )
{
	for ( ; *x != '>'; x++ ) {
		if ( ( *x >= 'A' ) && ( *x <= 'Z' ) )
			x[0] = (char)((int)(x[0]) + 32);
	}
}
/*
 * Output PostScript prolog code
 *
 * The main body of the prolog is read from a static data structure, which
 * contains the pagination macros. The data structure is in prolog.h, 
 * which is built by the Perl program mkincl. The source for the prolog is
 * paginate.ps. This is a "stripped" version of the PostScript code. The
 * human-readable source is in paginate.ps.verbose. This latter file is the
 * one which should be edited if PostScript code changes are required. The
 * Perl program pstrip converts paginate.ps.verbose to paginate.ps. The
 * purpose for doing all this is to make the program self contained, rather
 * than require shipping an extra file with it, containing the PostScript code.
 */
static time_t tloc;
void
prolog()
{
	char *c = &pscode[0];

	if(g.prolog) {

		puts("%!PS");
		puts("%Copyright (c) 1996 H&L Software, Inc.");
		puts("%All rights reserved");
		puts("%%BeginProlog");

		/*
		 * copy the PostScript macros from the static data
		 * structure to standard output.
		 */
		while( *c != 0 ) {
			putchar((int) *c++);
		}

		puts("\n%%EndProlog\n%%BeginSetup");

		/*
		 * set flag for drawing box (or not) around each page
		 */
		if(g.box)
			puts("/BOX true def\nDB	% draw box for first page"); 
		else
			puts("/BOX false def"); 

		/*
		 * set flag for running header (or not) 
		 */
		if(g.hdr) {
			puts("/HDR true def\n/PG 1 def"); 
			time(&tloc);
			printf("/MSG (Message converted on %s) def\n",  ctime(&tloc));
			puts("PH	% print header for first page"); 
		}
		else
			puts("/HDR false def"); 

		/*
		 * define short-hand literal names for fonts
		 */
		if (g.altFont) {
			strcpy(buff, "(Times-Roman) cvlit /f1 exch def ");
			strcat(buff, "(Times-Bold) cvlit /f1b exch def ");
			strcat(buff, "(Times-Italic) cvlit /f1i exch def ");
			strcat(buff, "(Times-BoldItalic) cvlit /f1bi exch def ");
		}
		else {
			strcpy(buff, "(Helvetica) cvlit /f1 exch def ");
			strcat(buff, "(Helvetica-Bold) cvlit /f1b exch def ");
			strcat(buff, "(Helvetica-Oblique) cvlit /f1i exch def ");
			strcat(buff, "(Helvetica-BoldOblique) cvlit /f1bi exch def ");
		}
		strcat(buff, "(Courier) cvlit /f2 exch def ");
		strcat(buff, "(Courier-Bold) cvlit /f2b exch def ");
		strcat(buff, "(Courier-Oblique) cvlit /f2i exch def ");
		strcat(buff, "(Courier-BoldOblique) cvlit /f2bi exch def\n");
		puts( buff );

		/*
		 * set the page margins
		 */
/*** to override the stuff in the PostScript prolog, this is the place ***/

		puts("%%EndSetup");
	}
}
/*
 * wrap up the PostScript output
 */
void
epilog()
{
	/*
	 * if text in the buffer, dump it out
	 */
	if (g.atMargin == 0)
		tokenOutput(buff);
	/*
	 * cause final "showpage"
	 */
	puts("/BOX false def\n/HDR false def"); 
	puts("NP");
	puts("%%EOF");
}
/*
 * this routine parses command line flags and arguments
 */
int
getArgs( int argc, char **argv )
{
	int c;
	int fs;
	int rc = 0;
	extern char     *optarg;
	extern int      optind;
	int		opterr = 0;

	/*
	 * get program name (for error msgs) and directory name (for
	 * finding PostScript prolog file.
	 */
	g.n = baseName( g.n, argv[0] );
	g.d = dirName( g.d, argv[0] );

	/*
	 * parse arguments
	 */
	while ((c=getopt(argc, argv, "bpts:h?")) != EOF)
		switch(c) {
			
		/*
		 * 'b' flag causes a box to be drawn around the margins
		 *	on each page.
		 */
		case 'b':
			g.box = 1;
			break;
		/*
		 * 'p' flag suppresses pre-pending the PostScript prolog
		 * 	to the output. this is useful when debugging, using
		 *	the program as a filter for standard input and 
		 *	interactively writing conversions to standard output.
		 */
		case 'p':
			g.prolog = 0;
			break;
		/*
		 * 't' flag changes default font to "Times",
		 *     rather than "Helvetica."
		 */
		case 't':
			g.altFont = 1;
			break;
		/*
		 * 'u' flag causes unrecognized MIME tags to be shown
		 *     in the output. useful for debugging.
		 */
		case 'u':
			g.showTags = 1;
			break;
		/*
		 * 's' flag followed by an integer overrides the
		 * default font size (10 pt)
		 */
		case 's':
			fs = atoi(optarg);
			if(errno)
				perror(g.n);
			if(fs > 0 && fs < 36) {
				g.fs = fs;
				g.ffs = fs;
			}
			break;
		/*
		 * 'h' flag causes printing of running headers
		 */
		case 'h':
			g.hdr = 1;
			break;
		case '?':
			opterr++;
			break;
		}
	if(opterr) {
		showHelp();
		rc = 1;
	}

	/*
	 * discard command line arguments
	 */
	for ( ; optind < argc; optind++) {
		fprintf(stderr, "%s: Unrecognized parameter: %s\n", g.n, argv[optind]);
		rc = 1;
	}
	return(rc);
}
/*
 * get program name, for error messages
 * simulate the "basename()" function, so as to avoid using libgen,
 *   since not always available.
 */
char *
baseName( char *n, char *a )
{
	extern char *strrchr( const char *, int);
	n = strrchr(a, (int)'/');
	if( n == NULL)
		n = a;
	else
		++n;
	return(n);
}
char *
dirName( char *n, char *a )
{
	char *end;
	extern char *strrchr( const char *, int);
	end = strrchr(a, (int)'/');
	if( end != NULL ) {
		strncpy(n, a, (int)(end-a+1));
		n[(int)(end-a+1)] = 0;
	}
	else
		n[0] = 0;
	return(n);
}
/*
 * display some help text
 */
void
showHelp()
{
	fprintf(stderr,"usage: %s [-b] [-p] [-t] [-h] [-s nn]\n",g.n);
	fprintf(stderr,"\nThe -b flag causes a box to be drawn along the page margins.\n");
	fprintf(stderr,"\nThe -p flag suppresses output of the PostScript prolog code, an option probably only useful for debugging.\n");
	fprintf(stderr,"\nThe -t flag changes the default font from Helvetica to Times-Roman.\n");
	fprintf(stderr,"\nThe -h flag causes running headers to be printed on each page.\n");
	fprintf(stderr,"\nThe -s flag changes the default font size from 10 pt to the value of \"nn\", up to a maximum of 36 pt.\n");
	fprintf(stderr,"\nThe -u flag causes unrecognized MIME tags to be shown in the output.\n");
}
