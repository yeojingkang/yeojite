/*** includes ***/
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#if defined __linux__ || defined unix || defined __unix || defined __unix__

#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>

#endif

#include "estring.h"

/*** defines ***/
#define EDITOR_VERSION "0.0.1 Pre-Alpha"

#define CTRL_KEY(k) (k & 0x1f)

enum editorKey
{
    ARROW_LEFT = 0x100,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN
};

/*** globals ***/
struct editorConfig
{
    int             cx, cy;      // The cursor's position
    int             termRows;
    int             termCols;
    struct termios  userTermios; // The user's original terminal attributes
} config;

/*** terminal ***/

void Exit(int retval)
{
    write(STDOUT_FILENO, "\x1b[2J", 4); // 2J (Erase In Display (4 byte): clear screen)
    write(STDOUT_FILENO, "\x1b[H", 3);  // H (Cursor Position (3 byte): <line>;<column>H)

    exit(retval);
}

void Die(const char* msg)
{
    perror(msg);
    Exit(1);
}

#if defined __linux__ || defined unix || defined __unix || defined __unix__

void SetCanonicalMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.userTermios))
        Die("Failed to set canonical mode (tcsetattr)");
}

void SetRawMode()
{
    if (tcgetattr(STDIN_FILENO, &config.userTermios))
        Die("Failed to set raw mode (tcgetattr)");

    // Makes the program call this function before exiting
    atexit(SetCanonicalMode);

    // Contains attributes for raw mode
    struct termios rawMode = config.userTermios;

    /*
     * Changes the following:
     * - Stop echoing input to the terminal
     * - Turn off canonical mode (No longer reading input line by line)
     * - Turn off signals (Ctrl-C, etc.)
     * - Turn off implementation-dependent functions from input
     */
    rawMode.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

    /*
     * Changes the following:
     *  - Disable start/stop output control (Ctrl-S, Ctrl-Q)
     *  - Disable mapping carriage return to newline on input
     *  - Disable SIGINT on break
     *  - Disable parity check
     *  - Disable stripping of each input byte's eighth bit
     */
    rawMode.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);

    rawMode.c_oflag &= ~(OPOST);    // Disable post-processing of output

    rawMode.c_cflag |= CS8;         // Set 8 bits per byte

    rawMode.c_cc[VMIN] = 0;         // Min number of bytes of input required
    rawMode.c_cc[VTIME] = 1;        // Max time (per 100ms) to wait for input

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &rawMode))
        Die("Failed to set raw mode (tcsetattr)");
}

int ReadKey()
{
    int readCount = 0;  // Number of bytes read
    char c = '\0';      // The character to return

    // Keep reading until there is an input
    while ((readCount = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (readCount == -1 && errno != EAGAIN)
            Die("Failed to read input (read)");
    }

    // Handle escape sequences
    if (c == '\x1b')
    {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1
         || read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b'; // User pressed ESC button

        if (seq[0] == '[')
        {
            switch (seq[1])
            {
            case 'A': return ARROW_UP;
            case 'B': return ARROW_DOWN;
            case 'C': return ARROW_RIGHT;
            case 'D': return ARROW_LEFT;
            }
        }

        return '\x1b';
    }

    return c;
}

int GetCursorPosition(int *rows, int* cols)
{
    char buf[32];
    unsigned int i = 0;

    // Request for cursor position
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    // Read the cursor position report (R is the last char of the report)
    while (i < sizeof(buf))
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1 || buf[i] == 'R')
            break;
        ++i;
    }
    buf[i] = '\0'; // Null terminate the string

    // Check if buffer contains the CPR
    if (buf[0] != '\x1b' && buf[1] != '[')
        return -1;

    // Read the contents into rows and cols
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return 0;
}

int GetTerminalSize(int* rows, int* cols)
{
    struct winsize winSz;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &winSz) == -1 || winSz.ws_col == 0)
    {
        // ioctl failed to retrieve terminal size

        // Move cursor to the bottom right of the terminal
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;

        return GetCursorPosition(rows, cols);
    }

    *cols = winSz.ws_col;
    *rows = winSz.ws_row;

    return 0;
}

/*** input ***/

void ProcessKey(int c)
{
    switch (c)
    {
    case CTRL_KEY('q'):
        Exit(0);
        break;

    // Cursor movement
    case 'h':
    case ARROW_LEFT:
        if (config.cx > 0) --config.cx;
        break;
    case 'j':
    case ARROW_DOWN:
        if (config.cy < config.termRows - 1) ++config.cy;
        break;
    case 'k':
    case ARROW_UP:
        if (config.cy > 0) --config.cy;
        break;
    case 'l':
    case ARROW_RIGHT:
        if (config.cx < config.termCols - 1) ++config.cx;
        break;
    }
}

/*** output ***/

void DrawRows(estring* buf)
{
    for (int y = 0; y < config.termRows; ++y)
    {
        if (y == config.termRows / 2) // Print the welcome message
        {
            char welcome[100];
            int len = snprintf(welcome, sizeof(welcome),
                               "YeojiTE version %s", EDITOR_VERSION);

            if (len > config.termCols)
                len = config.termCols;

            unsigned int padding = (config.termCols - len) / 2;

            if (padding)
            {
                estrAppend(buf, "~", 1);
                --padding;
            }

            // Add whitespaces for padding
            while (padding--) estrAppend(buf, " ", 1);

            estrAppend(buf, welcome, len);
        }
        //else if (y == config.termRows - 1) // Print the cursor position
        //{
        //    char pos[64];
        //    int len = snprintf(pos, sizeof(pos), "~ %d:%d", config.cx + 1, config.cy + 1);
        //    estrAppend(buf, pos, len);
        //}
        else
        {
            estrAppend(buf, "~", 1);
        }

        estrAppend(buf, "\x1b[K", 3); // Clear from cursor to end of row
        if (y < config.termRows - 1)
            estrAppend(buf, "\r\n", 2);
    }
}

void PrintScreen()
{
    // The content to print to the screen
    estring buffer = ESTR_INIT;

    /* 
     * Write escape sequences to the terminal.
     * Sequences start with: \x1b (ESC) + [
     */

    estrAppend(&buffer, "\x1b[?25l", 6); // ?25l (Hide cursor)
    //estrAppend(&buffer, "\x1b[2J", 4); // 2J (Erase In Display (4 byte): clear screen)
    estrAppend(&buffer, "\x1b[H", 3); // H (Cursor Position (3 byte): <line>;<column>H)

    DrawRows(&buffer);

    char cursorPos[32];
    int len = snprintf(cursorPos, sizeof(cursorPos),
                       "\x1b[%d;%dH", config.cy + 1, config.cx + 1);
    estrAppend(&buffer, cursorPos, len); // Place cursor at current position

    estrAppend(&buffer, "\x1b[?25h", 6); // ?25h (Show cursor)

    write(STDOUT_FILENO, buffer.s, buffer.len); // Print the buffer
    estrFree(&buffer);
}

#endif

/*** init ***/
void Init()
{
    config.cx = config.cy = 0;

    if (GetTerminalSize(&config.termRows, &config.termCols) == -1)
        Die("Failed to get terminal size (GetTerminalSize)");
}

int main()
{
#if defined __linux__ || defined unix || defined __unix || defined __unix__
    SetRawMode();
    Init();

    while (1)
    {
        PrintScreen();
        ProcessKey(ReadKey());
    }

    SetCanonicalMode();
#endif

    return 0;
}
