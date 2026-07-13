#include <iostream>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <format>

#define CTRL_KEY(k) ((k) & 0x1f)
const std::string g_version{"1.0"};

termios origTermios{};

void disableRawMode()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios);
}

void enableRawMode()
{
    tcgetattr(STDIN_FILENO, &origTermios);
    atexit(disableRawMode);

    termios raw{};
    tcgetattr(STDIN_FILENO, &raw);

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

class getWindowSize
{
private:
    winsize wSize{};

public:
    int getWSCol()
    {
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &wSize) == -1 || wSize.ws_col == 0)
        {
            return -1;
        }
        else
        {
            return wSize.ws_col;
        }
    }
    int getWSRows()
    {
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &wSize) == -1 || wSize.ws_col == 0)
        {
            return -1;
        }
        else
        {
            return wSize.ws_row;
        }
    }
};

struct EditorConf
{
    int cursorX{};
    int cursorY{};
};
EditorConf g_editorConf{};

namespace Editor
{

    enum Keys
    {
        up = 1000,
        down,
        left,
        right,
        pageUp,
        pageDown,
        homeKey,
        endKey,
    };

    namespace Buffer
    {
        std::string &getBuffer()
        {
            static std::string buf{};
            return buf;
        }

        void addToBuffer(const std::string &string)
        {
            getBuffer() += string;
        }

        void clearBuffer()
        {
            getBuffer().clear();
        }

    } // namespace Buffer

    void drawRows()
    {
        std::string helloMessage{"Hello! Version: " + g_version};

        for (int i = 0; i < getWindowSize().getWSRows(); i++)
        {
            if (i == getWindowSize().getWSRows() / 2)
            {
                int padding{(getWindowSize().getWSCol() - helloMessage.length()) / 2};
                if (padding)
                {
                    Buffer::addToBuffer("~");
                    padding--;
                }
                while (padding--)
                {
                    Buffer::addToBuffer(" ");
                }
                Buffer::addToBuffer(helloMessage);
            }
            else
                Buffer::addToBuffer("~");
            Buffer::addToBuffer("\x1b[K");
            if (i != getWindowSize().getWSRows() - 1)
                Buffer::addToBuffer("\r\n");
        }
    }

    void moveCursor(int key)
    {
        switch (key)
        {
        case up:
            if (g_editorConf.cursorY != 0)
                g_editorConf.cursorY--;
            break;

        case down:
            if (g_editorConf.cursorY != getWindowSize().getWSRows() - 1)
                g_editorConf.cursorY++;
            break;

        case left:
            if (g_editorConf.cursorX != 0)
                g_editorConf.cursorX--;
            break;

        case right:
            if (g_editorConf.cursorX != getWindowSize().getWSCol() - 1)
                g_editorConf.cursorX++;
            break;

        default:
            break;
        }
    }

    void refreshScreen()
    {
        Buffer::addToBuffer("\x1b[?25l");
        Buffer::addToBuffer("\x1b[H");
        drawRows();
        std::string cursorPosStr{std::format("\x1b[{};{}H", g_editorConf.cursorY + 1, g_editorConf.cursorX + 1)};
        Buffer::addToBuffer(cursorPosStr);
        Buffer::addToBuffer("\x1b[?25h");
        write(STDOUT_FILENO, Buffer::getBuffer().c_str(), Buffer::getBuffer().length());
        Buffer::clearBuffer();
    }

    int readKey()
    {
        int returnedInt{};
        char c{};
        while ((returnedInt = read(STDIN_FILENO, &c, 1)) != 1)
        {
            if (returnedInt == -1 && errno != EAGAIN)
            {
                std::cerr << "Error in u_Editor::readKey()\r\n";
                exit(EXIT_FAILURE);
            }
        }
        if (c == '\x1b')
        {
            char seq[3]{};
            if (read(STDIN_FILENO, &seq[0], 1) != 1)
                return c;
            if (read(STDIN_FILENO, &seq[1], 1) != 1)
                return c;
            if (seq[0] == '[')
            {
                if (seq[1] >= '0' && seq[1] <= '9')
                {
                    if (read(STDIN_FILENO, &seq[2], 1) != 1)
                        return c;
                    if (seq[2] == '~')
                    {
                        switch (seq[1])
                        {
                        case '1':
                        case '7':
                            return homeKey;
                        case '4':
                        case '8':
                            return endKey;
                        case '5':
                            return pageUp;
                        case '6':
                            return pageDown;

                        default:
                            break;
                        }
                    }
                }
                else
                {
                    switch (seq[1])
                    {
                    case 'A':
                        return up;
                    case 'B':
                        return down;
                    case 'C':
                        return right;
                    case 'D':
                        return left;
                    case 'H':
                        return homeKey;
                    case 'F':
                        return endKey;
                    default:
                        break;
                    }
                }
            }
            else if (seq[0] == 'O')
            {
                switch (seq[1])
                {
                case 'H':
                    return homeKey;
                case 'F':
                    return endKey;
                
                default:
                    break;
                }
            }
        }
        return c;
    }

    void processKeypress()
    {
        int c = readKey();
        switch (c)
        {
        case (CTRL_KEY('q')):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(EXIT_SUCCESS);
            break;

        case up:
        case down:
        case left:
        case right:
            moveCursor(c);
            break;

        case pageUp:
        case pageDown:
        {
            for (int i = getWindowSize().getWSRows(); i > 0; i--)
            {
                moveCursor(c == pageUp ? up : down);
            }
        }
        break;

        case homeKey:
            g_editorConf.cursorX = 0;
            break;
        case endKey:
            g_editorConf.cursorX = getWindowSize().getWSCol() - 1;
            break;
        
        default:
            break;
        }
    }

} // namespace u_Editor

int main()
{
    enableRawMode();
    while (true)
    {
        Editor::refreshScreen();
        Editor::processKeypress();
    }
    return EXIT_SUCCESS;
}