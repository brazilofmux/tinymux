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

class Output;
class Status;

class Input
{
public:
    Input(int lines, int columns, int yOrigin, int xOrigin, Output *pOut);
    bool Valid(void);
    int  Refresh(void);
    void AppendCharacter(wchar_t chin);
    void Backspace(void);
    bool EndOfLine(void);
    void Redraw(void);

private:
    WINDOW *m_w;
    int     m_lines;
    int     m_columns;
    wchar_t m_aBuffer[4000];
    size_t  m_nBufferTotal;
    size_t  m_nBufferToLeft;
    Output *m_pOut;
};

class Output
{
public:
    Output(int lines, int columns, int yOrigin, int xOrigin);
    bool Valid(void);
    int  Refresh(void);
    void AppendLine(size_t nLine, const wchar_t *pLine);
    void AppendLine(const wchar_t *pLine);

private:
    WINDOW *m_w;
    int     m_lines;
    int     m_columns;
};

class Status
{
public:
    Status(int lines, int columns, int yOrigin, int xOrigin);
    bool Valid(void);
    void Update(void);
    int  Refresh(void);

private:
    WINDOW *m_w;
    int     m_lines;
    int     m_columns;
};

Input::Input(int lines, int columns, int yOrigin, int xOrigin, Output *pOut)
{
    m_nBufferTotal = 0;
    m_nBufferToLeft = 0;
    m_pOut  = NULL;
    m_w = newwin(lines, columns, yOrigin, xOrigin);
    if (NULL != m_w)
    {
        m_lines   = lines;
        m_columns = columns;
        m_pOut = pOut;

        idlok(m_w, TRUE);
        scrollok(m_w, TRUE);
    }
}

bool Input::Valid(void)
{
    return (NULL != m_w);
}

int Input::Refresh(void)
{
    return wnoutrefresh(m_w);
}

void Input::Backspace(void)
{
    if (0 == m_nBufferToLeft)
    {
        // There are no characters to left we can remove with a backspace action.
        //
        return;
    }

    if (m_nBufferToLeft == m_nBufferTotal)
    {
        // We are positioned at the end of the buffer, so we may be able to
        // short-cut the erasure.  However, if we are close to the leftmost
        // column, we leave it to the general case.
        //
        int y, x;
        getyx(m_w, y, x);
        if (1 < x)
        {
            mvwdelch(m_w, y, x-1);
            m_nBufferTotal--;
            m_nBufferToLeft--;
            return;
        }
    }

    // General case.
    //
    m_nBufferTotal--;
    for (size_t i = m_nBufferToLeft - 1; i < m_nBufferTotal; i++)
    {
        m_aBuffer[i] = m_aBuffer[i+1];
    }
    m_nBufferToLeft--;

    Redraw();
}

void Input::Redraw(void)
{
    wmove(m_w, 0, 0);
    werase(m_w);
    //wcwidth(wchar_t)
    // TODO: Draw the buffer to the window.
}

bool Input::EndOfLine(void)
{
    m_aBuffer[m_nBufferTotal] = L'\0';
    if (wcscasecmp(L"/quit", m_aBuffer) == 0)
    {
        return true;
    }

    // Send line to output window.
    //
    m_pOut->AppendLine(m_nBufferTotal, m_aBuffer);
    m_nBufferTotal = 0;
    m_nBufferToLeft = 0;

    int x, y;
    getyx(m_w, y, x);
    if (m_lines-1 == y)
    {
        scroll(m_w);
        wmove(m_w, m_lines-1, 0);
    }
    else
    {
        wmove(m_w, y+1, 0);
    }
    return false;
}

void Input::AppendCharacter(wchar_t chin)
{
    wchar_t chtemp[2] = { L'\0', L'\0' };

    if (m_nBufferTotal < sizeof(m_aBuffer))
    {
        m_aBuffer[m_nBufferTotal] = chin;
        m_nBufferTotal++;
        m_nBufferToLeft++;

        cchar_t chout;
        chtemp[0] = chin;
        if (OK == setcchar(&chout, chtemp, A_NORMAL, 0, NULL))
        {
            (void)wadd_wch(m_w, &chout);
        }
    }
}

Output::Output(int lines, int columns, int yOrigin, int xOrigin)
{
    m_w  = newwin(lines, columns, yOrigin, xOrigin);
    if (NULL != m_w)
    {
        m_lines   = lines;
        m_columns = columns;
        idlok(m_w, TRUE);
        scrollok(m_w, TRUE);
    }
}

bool Output::Valid(void)
{
    return (NULL != m_w);
}

int Output::Refresh(void)
{
    return wnoutrefresh(m_w);
}

void Output::AppendLine(size_t nLine, const wchar_t *pLine)
{
    int y, x;
    getyx(m_w, y, x);
    if (m_lines-1 == y)
    {
        scroll(m_w);
        wmove(m_w, m_lines-1, 0);
    }
    else
    {
        wmove(m_w, y+1, 0);
    }

    wchar_t chtemp[2] = { L'\0', L'\0' };
    for (int i = 0; i < nLine && L'\0' != pLine[i]; i++)
    {
        cchar_t chout;
        chtemp[0] = pLine[i];
        if (OK == setcchar(&chout, chtemp, A_NORMAL, 0, NULL))
        {
            (void)wadd_wch(m_w, &chout);
        }
    }
}

void Output::AppendLine(const wchar_t *pLine)
{
    AppendLine(wcslen(pLine), pLine);
}

Status::Status(int lines, int columns, int yOrigin, int xOrigin)
{
    m_w = newwin(lines, columns, yOrigin, xOrigin);
    if (NULL != m_w)
    {
        m_lines   = lines;
        m_columns = columns;
    }
}

bool Status::Valid(void)
{
    return (NULL != m_w);
}

int Status::Refresh(void)
{
    return wnoutrefresh(m_w);
}

void Status::Update(void)
{
    wchar_t wchSpace[2] = { L' ', L'\0' };
    cchar_t cchSpace;
    (void)setcchar(&cchSpace, wchSpace, A_UNDERLINE, 0, NULL);
    wmove(m_w, 0, 0);
    int n = COLS;
    while (n--)
    {
        (void)wadd_wch(m_w, &cchSpace);
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

    Output output(LINES-3, COLS, 0, 0);
    Status status(1, COLS, LINES-3, 0);
    Input  input(2, COLS, LINES-2, 0, &output);

    if (  !output.Valid()
       || !status.Valid()
       || !input.Valid())
    {
        endwin();
        fprintf(stderr, "Could not create a window.\r\n");
        return 1;
    }

    wchar_t chtemp[2] = { L'\0', L'\0' };

    output.AppendLine(L"Hello, World.");

    status.Update();

    for (;;)
    {
        if (  ERR == output.Refresh()
           || ERR == status.Refresh()
           || ERR == input.Refresh()
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
                input.Backspace();
            }
            else
            {
                const int nBuffer = 100;
                wchar_t buffer[nBuffer];
                swprintf(buffer, nBuffer-1, L"[0x%08X]", chin);
                output.AppendLine(buffer);
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
                    if (input.EndOfLine())
                    {
                        break;
                    }
                }
            }
            else if (iswprint(chin))
            {
                // Printable character.
                //
                input.AppendCharacter(chin);
            }
        }
    }
    endwin();

    return 0;
}
