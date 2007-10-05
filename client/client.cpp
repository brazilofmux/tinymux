#include <stdio.h>
#include <ncursesw/ncurses.h>
#include <locale.h>

// TODO:
//
// Add sockets.
// Add telnet negotiation.
// Add conversions to and from wint_t to UTF-8.
// Add SSL
// Add configure.in
//

WINDOW *g_scrOutput  = NULL;
WINDOW *g_scrStatus  = NULL;
WINDOW *g_scrInput   = NULL;

cchar_t chLine;

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    initscr();
    if (  COLS < 10
       || LINES < 10)
    {
        // Too small.
        //
        endwin();
        fprintf(stderr, "Window is less than 10x10.\r\n");
        return 1;
    }

    raw();
    cbreak();
    noecho();
    nonl();
    intrflush(stdscr, FALSE);
    refresh();

    WINDOW *g_scrOutput  = newwin(LINES - 3, COLS, 0, 0);
    WINDOW *g_scrStatus  = newwin(1, COLS, LINES-3, 0);
    WINDOW *g_scrInput   = newwin(2, COLS, LINES-2, 0);
    if (  NULL == g_scrOutput
       || NULL == g_scrStatus
       || NULL == g_scrInput)
    {
        endwin();
        fprintf(stderr, "Could not create a window.\r\n");
        return 1;
    }

    keypad(g_scrInput, TRUE);

    wchar_t chtemp[2] = { L'\0', L'\0' };
    chtemp[0] = ' ';
    (void)setcchar(&chLine, chtemp, A_UNDERLINE, 0, NULL);

    waddstr(g_scrOutput, "Hello World !!!");

    int n = COLS;
    while (n--)
    {
        (void)wadd_wch(g_scrStatus, &chLine);
    }

    for (;;)
    {
        if (  ERR == wnoutrefresh(g_scrOutput)
           || ERR == wnoutrefresh(g_scrStatus)
           || ERR == wnoutrefresh(g_scrInput)
           || ERR == doupdate())
        {
            break;
        }

        wint_t chin;
        cchar_t chout;
        int cc = get_wch(&chin);
        if (KEY_CODE_YES == cc)
        {
            // Function key pressed.
            //
            wprintw(g_scrOutput, "[0x%08X]", chin);
        }
        else if (OK == cc)
        {
            // Normal character.
            //
            wprintw(g_scrOutput, "(0x%08X)", chin);
            if ('n' == chin)
            {
                break;
            }
          
            chtemp[0] = chin;
            if (OK == setcchar(&chout, chtemp, A_NORMAL, 0, NULL))
            {
                (void)wadd_wch(g_scrOutput, &chout);
                (void)wadd_wch(g_scrInput, &chout);
            }
        }
        else
        {
            break;
        }
    }
    endwin();

    return 0;
}
