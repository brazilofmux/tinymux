// ansi.h
//
// $Id: ansi.h,v 1.1 2003/01/22 19:58:25 sdennis Exp $ */
//
// ANSI control codes for various neat-o terminal effects
//
// Some older versions of Ultrix don't appear to be able to handle these
// escape sequences. If lowercase 'a's are being stripped from @doings,
// and/or the output of the ANSI flag is screwed up, you have the Ultrix
// problem.
//

#ifndef _ANSI_H
#define _ANSI_H

#define BEEP_CHAR     '\07'
#define ESC_CHAR      '\033'
#define ANSI_ATTR_CMD 'm'

#define ANSI_NORMAL   "\033[0m"

#define ANSI_HILITE   "\033[1m"
#define ANSI_UNDER    "\033[4m"
#define ANSI_BLINK    "\033[5m"
#define ANSI_INVERSE  "\033[7m"

// Foreground colors.
//
#define ANSI_FOREGROUND "\033[3"
#define ANSI_BLACK      "\033[30m"
#define ANSI_RED        "\033[31m"
#define ANSI_GREEN      "\033[32m"
#define ANSI_YELLOW     "\033[33m"
#define ANSI_BLUE       "\033[34m"
#define ANSI_MAGENTA    "\033[35m"
#define ANSI_CYAN       "\033[36m"
#define ANSI_WHITE      "\033[37m"

// Background colors.
//
#define ANSI_BACKGROUND "\033[4"
#define ANSI_BBLACK     "\033[40m"
#define ANSI_BRED       "\033[41m"
#define ANSI_BGREEN     "\033[42m"
#define ANSI_BYELLOW    "\033[43m"
#define ANSI_BBLUE      "\033[44m"
#define ANSI_BMAGENTA   "\033[45m"
#define ANSI_BCYAN      "\033[46m"
#define ANSI_BWHITE     "\033[47m"

#endif // !_ANSI_H
