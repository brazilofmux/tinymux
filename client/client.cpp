#include <stdio.h>
#include <ncursesw/ncurses.h>
#include <locale.h>

// TODO:
//
// Create two sub-windows, one for output and one for input.
// Add sockets.
// Add telnet negotiation.
// Add conversions to and from wint_t to UTF-8.
// Add SSL
// Add configure.in
//
int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    initscr();
    raw();
    keypad(stdscr, TRUE);
    cbreak();
    noecho();
    nonl();
    intrflush(stdscr, FALSE);
    printw("Hello World !!!");

    for (;;)
    {
        refresh();
        wint_t chin;
        wchar_t chtemp[2];
        cchar_t chout;
        int cc = get_wch(&chin);
        char buffer[100];
        if (KEY_CODE_YES == cc)
        {
            // Function key pressed.
            sprintf(buffer, "[0x%08X]", chin);
            printw(buffer);
        }
        else if (OK == cc)
        {
            // Normal character.
            //
            sprintf(buffer, "(0x%08X)", chin);
            printw(buffer);
            if ('n' == chin)
            {
                break;
            }
          
            chtemp[0] = chin;
            chtemp[1] = L'\0';
            if (OK == setcchar(&chout, chtemp, A_NORMAL, 0, NULL))
            {
                if  (ERR == add_wch(&chout))
                {
                    break;
                }
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
