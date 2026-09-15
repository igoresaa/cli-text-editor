#include <iostream>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <format>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <print>

/*** Энум а ваще типы тут и всякое ***/
enum class Keys : int {
    Up = 1000,
    Down,
    Left,
    Right,
    PageUp,
    PageDown,
    HomeKey,
    EndKey,
    DeleteKey,
};

struct Cursor {
    int col{};
    int row{};
};

constexpr char cntrlKey(const char x) {
    return static_cast<char>(x & 0x1f);
}

/*** Настройка терминала ***/
class RawMode {
public:
    RawMode() {
        if (tcgetattr(STDIN_FILENO, &sourceTerm_) == -1)
            throw std::runtime_error("tcgetattr(sourceTerm) in RawMode"
                " failed");
        termios raw{sourceTerm_};

        /* включаем / выключаем нужные флаги;
         * &= ~ это выключить
         * |= это включить
         */

        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= (CS8);
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
            throw std::runtime_error("tcsetattr(raw) failed");
    }

    ~RawMode() {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &sourceTerm_);
    }

private:
    termios sourceTerm_{};
};

/*** Короче все главные функции итд ***/
class OutputBuffer {
public:
    void append(const std::string_view string) {
        buffer_ += string;
    }

    void flushToSTDOUT() {
        write(STDOUT_FILENO, buffer_.data(), buffer_.size());
        buffer_.clear();
    }

private:
    std::string buffer_;
};

class Editor {
public:
    // TODO: сделать открытие файлов
    Editor(const std::string &filename) {
        openFile(filename);
        refreshScreen();
        while (isActive_) {
            processKeypress();
            refreshScreen();
        }
    }

    ~Editor() {
        output_.append("\x1b[2J");
        output_.append("\x1b[H");
        output_.flushToSTDOUT();
    }

    void openFile(const std::string &filename) {
        fileContents_.clear();

        std::ifstream file{filename};
        if (!file) throw std::runtime_error("openFile() failed");
        std::string line{};
        while (std::getline(file, line)) {
            fileContents_.push_back(line);
        }
    }

private:
    // переменные
    bool isActive_{true};
    winsize wSize_{};
    OutputBuffer output_{};
    Cursor cursor_{};
    std::vector<std::string> fileContents_{};
    int n_{};

    // функции
    void refreshScreen() {
        // output_.append("\x1b[?25l");
        output_.append("\x1b[H");
        drawRows();
        output_.append(
            std::format("\x1b[{};{}H",
                        cursor_.row + 1,
                        cursor_.col + 1)
        );
        output_.flushToSTDOUT();
    }

    static int readKey() {
        char c{};
        int x{};
        while ((x = static_cast<int>(read(STDIN_FILENO, &c, 1))) != 1) {
            if (x == -1 && errno != EAGAIN) {
                throw std::runtime_error("readKey() in Editor::readKey() "
                    "failed");
            }
        }

        if (c == '\x1b') {
            char seq[3]{};
            if (read(STDIN_FILENO, &seq[0], 1) != 1) return c;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) return c;
            if (seq[0] == '[') {
                if (seq[1] >= '0' && seq[1] <= '9') {
                    if (read(STDIN_FILENO, &seq[2], 1) != 1) return c;
                    if (seq[2] == '~') {
                        switch (seq[1]) {
                            case '1':
                            case '7': return static_cast<int>(Keys::HomeKey);
                            case '4':
                            case '8': return static_cast<int>(Keys::EndKey);
                            case '5': return static_cast<int>(Keys::PageUp);
                            case '6': return static_cast<int>(Keys::PageDown);
                            case '3': return static_cast<int>(Keys::DeleteKey);

                            default:
                                break;
                        }
                    }
                } else {
                    switch (seq[1]) {
                        case 'A': return static_cast<int>(Keys::Up);
                        case 'B': return static_cast<int>(Keys::Down);
                        case 'C': return static_cast<int>(Keys::Right);
                        case 'D': return static_cast<int>(Keys::Left);
                        case 'H': return static_cast<int>(Keys::HomeKey);
                        case 'F': return static_cast<int>(Keys::EndKey);

                        default:
                            break;
                    }
                }
            } else if (seq[0] == 'O') {
                switch (seq[1]) {
                    case 'H': return static_cast<int>(Keys::HomeKey);
                    case 'F': return static_cast<int>(Keys::EndKey);

                    default:
                        break;
                }
            }
        }
        return c;
    }

    void processKeypress() {
        switch (int c{readKey()}) {
            case cntrlKey('q'): isActive_ = false;
                break;
            case static_cast<int>(Keys::HomeKey): cursor_.col = 0;
                break;
            case static_cast<int>(Keys::EndKey): cursor_.col = screenCols() - 1;
                break;
            case static_cast<int>(Keys::Up):
            case static_cast<int>(Keys::Down):
            case static_cast<int>(Keys::Left):
            case static_cast<int>(Keys::Right):
                moveCursor(static_cast<Keys>(c));
                break;
            case static_cast<int>(Keys::PageUp):
            case static_cast<int>(Keys::PageDown):
                for (int i = screenRows(); i > 0; --i) {
                    moveCursor(static_cast<Keys>(
                        c == static_cast<int>(Keys::PageUp) ? Keys::Up : Keys::Down
                    ));
                }

            default:
                break;
        }
    }

    [[nodiscard]] int screenRows() const {
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &wSize_) == -1 ||
            wSize_.ws_row == 0) {
            return -1;
        } else {
            return wSize_.ws_row;
        }
    }

    [[nodiscard]] int screenCols() const {
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &wSize_) == -1 ||
            wSize_.ws_col == 0) {
            return -1;
        } else {
            return wSize_.ws_col;
        }
    }

    void drawRows() {
        output_.append("\x1b[2J");
        const int rows{screenRows()};
        for (int i = 0; i < rows; ++i) {
            if (i + n_ >= fileContents_.size()) {
                output_.append("~");
            } else {
                output_.append(fileContents_[i + n_]);
            }
            if (i != rows - 1) output_.append("\r\n");
        }
    }

    void moveCursor(const Keys key) {
        switch (key) {
            case Keys::Up:
                if (cursor_.row != 0) --cursor_.row;
                else if (n_ != 0) --n_;
                break;
            case Keys::Down:
                if (cursor_.row != screenRows() - 1) ++cursor_.row;
                else if (n_ < static_cast<int>(fileContents_.size()) - screenRows()) ++n_;
                break;
            case Keys::Left: if (cursor_.col != 0) --cursor_.col;
                break;
            case Keys::Right:
                if (cursor_.col != screenCols() - 1) ++cursor_.col;
                break;
            default: break;
        }
    }
};

int main(const int argc, char *argv[]) {
    std::string filename{};
    if (argc > 1) {
        filename = argv[1];
    }
    RawMode rawMode{};
    Editor editor{filename};
    return 0;
}
