#include <stdio.h>
#include <wchar.h>
#include <wctype.h>
#include <ncursesw/ncurses.h>
#include <locale.h>

// TODO:
//
// Add sockets.
// Add telnet negotiation.
// Add conversions to and from wint_t to UTF-8.
// Add SSL
// Add configure.in
// Add scrollback in output window.
//

WINDOW *g_scrOutput  = NULL;
WINDOW *g_scrStatus  = NULL;
WINDOW *g_scrInput   = NULL;

void UpdateStatusWindow(void)
{
    wchar_t wchSpace[2] = { L' ', L'\0' };
    cchar_t cchSpace;
    (void)setcchar(&cchSpace, wchSpace, A_UNDERLINE, 0, NULL);
    wmove(g_scrStatus, 0, 0);
    int n = COLS;
    while (n--)
    {
        (void)wadd_wch(g_scrStatus, &cchSpace);
    }
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    initscr();
    keypad(stdscr, TRUE);

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
    noecho();
    nonl();
    idlok(stdscr, TRUE);
    scrollok(stdscr, TRUE);
    intrflush(stdscr, FALSE);
    timeout(50);
    refresh();

    g_scrOutput  = newwin(LINES - 3, COLS, 0, 0);
    g_scrStatus  = newwin(1, COLS, LINES-3, 0);
    g_scrInput   = newwin(2, COLS, LINES-2, 0);
    if (  NULL == g_scrOutput
       || NULL == g_scrStatus
       || NULL == g_scrInput)
    {
        endwin();
        fprintf(stderr, "Could not create a window.\r\n");
        return 1;
    }
    idlok(g_scrOutput, TRUE);
    scrollok(g_scrOutput, TRUE);
    idlok(g_scrInput, TRUE);
    scrollok(g_scrInput, TRUE);

    wchar_t chtemp[2] = { L'\0', L'\0' };

    waddstr(g_scrOutput, "Hello World !!!");
    wmove(g_scrOutput, 1, 0);

    UpdateStatusWindow();

    wchar_t aBuffer[8000];
    size_t  nBuffer = 0;

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
            if (KEY_BACKSPACE == chin)
            {
                if (0 < nBuffer)
                {
                    nBuffer--;
                }
                int y, x;
                getyx(g_scrInput, y, x);
                mvwdelch(g_scrInput, y, x-1);
            }
            else
            {
                wprintw(g_scrOutput, "[0x%08X]", chin);
            }
        }
        else if (OK == cc)
        {
            // Normal character.
            //
            if (iswcntrl(chin))
            {
                // Control character.
                //
                if (L'\r' == chin)
                {
                    aBuffer[nBuffer] = L'\0';
                    if (wcscasecmp(L"/quit", aBuffer) == 0)
                    {
                        break;
                    }

                    // Send line to output window.
                    //
                    for (int i = 0; i < nBuffer; i++)
                    {
                        chtemp[0] = aBuffer[i];
                        if (OK == setcchar(&chout, chtemp, A_NORMAL, 0, NULL))
                        {
                            (void)wadd_wch(g_scrOutput, &chout);
                        }
                    }
                    int y, x;
                    getyx(g_scrOutput, y, x);
                    if (LINES-3 == y)
                    {
                        scroll(g_scrOutput);
                        wmove(g_scrOutput, LINES-3, 0);
                    }
                    else
                    {
                        wmove(g_scrOutput, y+1, 0);
                    }

                    nBuffer = 0;

                    getyx(g_scrInput, y, x);
                    if (1 == y)
                    {
                        scroll(g_scrInput);
                        wmove(g_scrInput, 1, 0);
                    }
                    else
                    {
                        wmove(g_scrInput, y+1, 0);
                    }
                }
            }
            else if (iswprint(chin))
            {
                // Printable character.
                //
                if (nBuffer < sizeof(aBuffer))
                {
                    aBuffer[nBuffer++] = chin;

                    chtemp[0] = chin;
                    if (OK == setcchar(&chout, chtemp, A_NORMAL, 0, NULL))
                    {
                        (void)wadd_wch(g_scrInput, &chout);
                    }
                }
            }
        }
    }
    endwin();

    return 0;
}
