/*** includes ***/

// Feature test macros
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#if defined __linux__ || defined unix || defined __unix || defined __unix__

#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#endif

#include "estring.h"

/*** defines ***/
#define EDITOR_VERSION "0.0.2 Prototype"

#define CTRL_KEY(k) (k & 0x1f)

enum editorKey
{
    ARROW_LEFT = 0x100,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    KEY_HOME,
    KEY_END,
    KEY_DEL
};

typedef struct erow
{
    int size;
    char* str;
} erow;

/*** globals ***/
struct editorConfig
{
    int             cx, cy;      // The cursor's position
    int             termRows;
    int             termCols;

    int             numRows;     // The number of rows in the buffer
    erow*           row;

    int             currRow;     // The current position of the top row

    struct termios  userTermios; // The user's original terminal attributes
} config;

/*** terminal ***/

void Exit(int retval)
{
    write(STDOUT_FILENO, "\x1b[2J", 4); // 2J (Erase In Display (4 byte): clear screen)
    write(STDOUT_FILENO, "\x1b[H", 3);  // H (Cursor Position (3 byte): <line>;<column>H)

    write(STDOUT_FILENO, "\033c", 2);   // c (Clear screen(2 byte); for Windows terminal)

    // Free allocated data
    if (config.row)
    {
        while (config.numRows--)
        {
            if (config.row[config.numRows].str)
                free(config.row[config.numRows].str);
        }
        free(config.row);
    }

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
    /**************************************************************************/
    if (c == '\x1b')
    {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1
         || read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b'; // User pressed ESC button

        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';

                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                        case '1':
                        case '7': return KEY_HOME;
                        case '4':
                        case '8': return KEY_END;
                        case '3': return KEY_DEL;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return KEY_HOME;
                    case 'F': return KEY_END;
                }
            }
        }
        else if (seq[0] == 'O')
        {
            switch (seq[1])
            {
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
            }
        }

        return '\x1b';
    }
    /**************************************************************************/

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
    if (buf[0] != '\x1b' || buf[1] != '[')
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

/*** row ***/

void AppendRow(char* str, int len)
{
    erow* newRows = realloc(config.row, sizeof(erow) * (config.numRows + 1));

    if (newRows == NULL)
        Die("Failed to realloc rows (AppendRow)");

    config.row = newRows;

    // Copy string content
    config.row[config.numRows].size = len;
    config.row[config.numRows].str = malloc(len + 1);

    if (config.row[config.numRows].str == NULL)
        Die("Failed to allocate memory for new row (AppendRow)");

    memcpy(config.row[config.numRows].str, str, len);
    config.row[config.numRows].str[len] = '\0';
    ++config.numRows;
}

/*** file i/o ***/

void OpenFile(char* file)
{
    FILE* fp = fopen(file, "r");

    if (fp == NULL)
        Die("Failed to open file (fopen)");

    char* line = NULL;  // The next line in the file
    size_t cap = 0;     // The max number of chars to read (Param scapegoat)
    ssize_t len = 0;    // Length of the read line

    while ((len = getline(&line, &cap, fp)) != -1)
    {
        // Count number of chars in the line (excluding \r\n)
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            --len;

        AppendRow(line, len);
    }

    free(line);
    fclose(fp);
}

/*** input ***/

void ProcessKey(int c)
{
    erow* row = (config.cy >= config.numRows) ? NULL : &config.row[config.cy];
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
            if (config.cy < config.numRows - 1) ++config.cy;
            break;
        case 'k':
        case ARROW_UP:
            if (config.cy > 0) --config.cy;
            break;
        case 'l':
        case ARROW_RIGHT:
            if (row && config.cx < row->size) ++config.cx;
            break;

        case PAGE_UP:
            config.cy -= config.termRows;
            config.currRow -= config.termRows;
            if (config.cy < 0) config.cy = 0;
            if (config.currRow < 0)
            {
                config.currRow = 0;
                config.cy = 0; // TODO: Behaviour not exactly correct
            }
            break;
        case PAGE_DOWN:
            config.cy += config.termRows;
            config.currRow += config.termRows;
            if (config.cy >= config.numRows) config.cy = config.numRows - 1;
            if (config.currRow >= config.numRows - config.termRows)
            {
                config.currRow = config.numRows - config.termRows - 1;
                config.cy = config.numRows - 1; // TODO: Behaviour not exactly correct
            }
            break;

        case KEY_HOME:
            config.cx = 0;
            break;
        case KEY_END:
            config.cx = config.row[config.currRow + config.cy].size;
            break;


        // Text editing
        case '\b':
            break;
    }

    // Prevent cursor from going out of the current row
    row = (config.cy >= config.numRows) ? NULL : &config.row[config.cy];
    int len = row ? row->size : 0;
    if (config.cx > len)
        config.cx = len;
}

/*** output ***/

void DrawRows(estring* buf)
{
    for (int y = 0; y < config.termRows; ++y)
    {
        int rowIndex = y + config.currRow;

        if (rowIndex >= config.numRows)
        {
            if (config.numRows == 0 && y == config.termRows / 2) // Print the welcome message
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
            else
            {
                estrAppend(buf, "~", 1);
            }
        }
        else
        {
            // Print config.row's content
            int len = (config.row[rowIndex].size > config.termCols) ? config.termCols
                                                                   : config.row[rowIndex].size;

            estrAppend(buf, config.row[rowIndex].str, len);
        }

        estrAppend(buf, "\x1b[K", 3); // Clear from cursor to end of row

        if (y < config.termRows)
            estrAppend(buf, "\r\n", 2); // Move cursor to next line
    }

    // Print the cursor position
    char pos[64];
    int len = snprintf(pos, sizeof(pos),
                       "%3d:%3d", config.cx, config.cy);
    estrAppend(buf, pos, len);
}

void UpdateScroll()
{
    if (config.cy < config.currRow)
        config.currRow = config.cy;
    else if (config.cy >= config.currRow + config.termRows)
        config.currRow = config.cy - config.termRows + 1;
}

void PrintScreen()
{
    UpdateScroll();

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
                       "\x1b[%d;%dH", (config.cy - config.currRow) + 1, config.cx + 1);
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
    config.numRows = 0;
    config.row = NULL;
    config.currRow = 10;

    if (GetTerminalSize(&config.termRows, &config.termCols) == -1)
        Die("Failed to get terminal size (GetTerminalSize)");

    --config.termRows; // Reserve last row for debug printing
}

int main(int argc, char* argv[])
{
#if defined __linux__ || defined unix || defined __unix || defined __unix__
    SetRawMode();
    Init();

    if (argc > 1)
    {
        OpenFile(argv[1]);
    }

    while (1)
    {
        PrintScreen();
        ProcessKey(ReadKey());
    }

    SetCanonicalMode();
#endif

    return 0;
}
