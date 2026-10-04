#pragma once

// cuffsh's own prompt editor.
//
//   Enter          run what is typed (the whole buffer, exactly as typed)
//   Shift+Enter    insert a newline and keep editing (Alt+Enter also works,
//                  for terminals that can't report Shift+Enter)
//   <- -> Home End move the cursor; Backspace / Delete edit; Ctrl+A/E/K/U/W/L
//                  and Ctrl/Alt+arrow word movement work like in a shell
//   Up / Down      move between lines of a multi-line entry; at the first/last
//                  line they step through previous entries
//   Ctrl+C         cancel what is typed;   Ctrl+D / Ctrl+Z on an empty prompt
//                  leaves the shell
//
// There is deliberately no "this looks unfinished, wait for more" guessing:
// the engine is handed exactly what was submitted and reports its own error
// if that is incomplete or wrong.
//
// How keys are read:
//  * Windows: raw console key events (ReadConsoleInputW), which carry the
//    Shift state of Enter directly — no dependence on the terminal app.
//  * POSIX: bytes from a raw-mode tty. Plain terminals can't tell Shift+Enter
//    from Enter, so on startup we ask for the "Kitty keyboard protocol"
//    (CSI u) and xterm's modifyOtherKeys; terminals that support either
//    report Shift+Enter distinctly, the rest ignore the request and Shift+
//    Enter behaves like Enter there (Alt+Enter is the fallback).
//    Bracketed paste is requested too, so pasted multi-line code arrives as
//    one block instead of being run line by line.
//
// Drawing is a full redraw (one write) after each change, using a model of
// how the terminal lays text out (see layoutRows) so wrapped long lines and
// double-width characters (Korean/CJK) are erased and repositioned correctly.

#include "../engine/common/Limits.h"
#include "Platform.h"
#include "Terminal.h"
#include "Text.h"
#include "Width.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <cerrno>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#endif

namespace cuff::cli
{

    enum class EntryStatus
    {
        Submitted,
        Cancelled, // Ctrl+C
        Eof        // Ctrl+D / Ctrl+Z on an empty prompt, or the terminal went away
    };

    namespace detail
    {
        struct Pos
        {
            int row = 0;
            int col = 0; // may equal `cols`: the terminal's "pending wrap" state
        };

        // Decodes one UTF-8 sequence at s[i]; invalid bytes count as U+FFFD, 1 byte.
        inline size_t decodeAt(const std::string &s, size_t i, char32_t &cp)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80)
            {
                cp = c;
                return 1;
            }
            size_t extra = 0;
            if ((c & 0xE0) == 0xC0)
                extra = 1, cp = c & 0x1Fu;
            else if ((c & 0xF0) == 0xE0)
                extra = 2, cp = c & 0x0Fu;
            else if ((c & 0xF8) == 0xF0)
                extra = 3, cp = c & 0x07u;
            else
            {
                cp = 0xFFFD;
                return 1;
            }
            if (i + extra >= s.size())
            {
                cp = 0xFFFD;
                return 1;
            }
            for (size_t k = 1; k <= extra; ++k)
            {
                const unsigned char cc = static_cast<unsigned char>(s[i + k]);
                if ((cc & 0xC0) != 0x80)
                {
                    cp = 0xFFFD;
                    return 1;
                }
                cp = (cp << 6) | (cc & 0x3Fu);
            }
            return extra + 1;
        }

        struct Row
        {
            std::string text;        // what to print for this screen row (prompt included where one starts)
            bool wrapsToNext = false; // row ended because the line wrapped (the terminal moves down by itself)
        };

        struct RowsLayout
        {
            std::vector<Row> rows;
            Pos end;    // where the terminal cursor sits after printing every row
            Pos cursor; // where the editing cursor should be shown
        };

        // Models how a terminal `cols` wide draws the entry, row by row: a
        // 3-column prompt before each logical line, text wrapping at the right
        // edge (a double-width character that doesn't fit moves to the next row
        // whole), and the delayed-wrap behaviour where a line that exactly fills
        // a row leaves the cursor on that row's last column until more is
        // written. Printing the rows one after another, with a line break only
        // where wrapsToNext is false, reproduces exactly what typing the buffer
        // would draw — and lets the caller print just a slice of them.
        inline RowsLayout layoutRows(const std::string &buf, size_t cursor, int cols, const std::string &primary,
                                     const std::string &cont, int promptWidth = 3)
        {
            cols = std::max(cols, promptWidth + 1);
            RowsLayout r;
            r.rows.emplace_back();
            int row = 0, col = 0;
            auto startLine = [&](const std::string &prompt)
            {
                r.rows[row].text += prompt;
                col = promptWidth;
            };
            auto place = [&](int w, const char *data, size_t len)
            {
                if (col + w > cols)
                {
                    r.rows[row].wrapsToNext = true;
                    r.rows.emplace_back();
                    ++row;
                    col = 0;
                }
                r.rows[row].text.append(data, len);
                col += w;
            };

            startLine(primary);
            size_t i = 0;
            bool cursorSet = false;
            for (;;)
            {
                if (i == cursor && !cursorSet)
                {
                    cursorSet = true;
                    if (i < buf.size() && buf[i] != '\n')
                    {
                        char32_t cp;
                        decodeAt(buf, i, cp);
                        const int w = cpWidth(cp);
                        r.cursor = (col + w > cols) ? Pos{row + 1, 0} : Pos{row, col};
                    }
                    else
                    {
                        r.cursor = Pos{row, std::min(col, cols - 1)};
                    }
                }
                if (i >= buf.size())
                    break;
                if (buf[i] == '\n')
                {
                    r.rows.emplace_back();
                    ++row;
                    startLine(cont);
                    ++i;
                    continue;
                }
                char32_t cp;
                const size_t len = decodeAt(buf, i, cp);
                place(cpWidth(cp), buf.data() + i, len);
                i += len;
            }
            r.end = Pos{row, col};
            return r;
        }
    } // namespace detail

    class LineEditor
    {
    public:
        explicit LineEditor(Terminal &term) : term_(term) {}

        LineEditor(const LineEditor &) = delete;
        LineEditor &operator=(const LineEditor &) = delete;

        // Raw mode (and, on POSIX, the extra key-reporting requests) is held
        // only for the duration of one call: between entries the engine may run
        // a script that calls input(), which needs the terminal back in its
        // normal echoing state, and a Ctrl+C during that execution must reach
        // the signal handler in Terminal.h instead of being read here.
        EntryStatus readEntry(std::string &out)
        {
            if (!canEditInPlace())
                return readSimple(out);
            RawModeScope raw(*this);
            if (!raw.ok)
                return readSimple(out);

            buffer_.clear();
            cursor_ = 0;
            curRow_ = 0;
            viewTop_ = 0;
            firstRender_ = true;
            dirty_ = true;
            historyPos_ = history_.size();
            draft_.clear();
            inPaste_ = false;

            for (;;)
            {
                // Redraw only once there is no more queued input (a big paste
                // is then one redraw, not one per character), and always
                // before blocking for the next key.
                if (dirty_ && !inputPending())
                {
                    render();
                    dirty_ = false;
                }

                const Key k = nextKey();
                switch (k.kind)
                {
                case Kind::Char:
                    insertCodepoint(k.cp);
                    break;

                case Kind::Tab:
                    insertText("    ");
                    break;

                case Kind::ShiftEnter:
                    insertText("\n");
                    break;

                case Kind::Enter:
                    inPaste_ = false;
                    if (isBlank(buffer_))
                    {
                        finishLine(false);
                        buffer_.clear();
                        cursor_ = 0;
                        curRow_ = 0;
                        firstRender_ = true;
                        dirty_ = true;
                        historyPos_ = history_.size();
                        draft_.clear();
                        break;
                    }
                    finishLine(true);
                    out = buffer_;
                    if (history_.empty() || history_.back() != buffer_)
                    {
                        history_.push_back(buffer_);
                        if (history_.size() > kMaxHistory)
                            history_.erase(history_.begin());
                    }
                    return EntryStatus::Submitted;

                case Kind::Backspace:
                    if (cursor_ > 0)
                    {
                        const size_t p = prevBoundary(cursor_);
                        buffer_.erase(p, cursor_ - p);
                        cursor_ = p;
                        dirty_ = true;
                    }
                    break;

                case Kind::Delete:
                    deleteForward();
                    break;

                case Kind::Left:
                    if (cursor_ > 0)
                    {
                        cursor_ = prevBoundary(cursor_);
                        dirty_ = true;
                    }
                    break;

                case Kind::Right:
                    if (cursor_ < buffer_.size())
                    {
                        cursor_ = nextBoundary(cursor_);
                        dirty_ = true;
                    }
                    break;

                case Kind::Home:
                    cursor_ = lineStart(cursor_);
                    dirty_ = true;
                    break;

                case Kind::End:
                    cursor_ = lineEnd(cursor_);
                    dirty_ = true;
                    break;

                case Kind::WordLeft:
                    cursor_ = wordLeft(cursor_);
                    dirty_ = true;
                    break;

                case Kind::WordRight:
                    cursor_ = wordRight(cursor_);
                    dirty_ = true;
                    break;

                case Kind::KillToEnd:
                {
                    const size_t e = (cursor_ < buffer_.size() && buffer_[cursor_] == '\n') ? cursor_ + 1 : lineEnd(cursor_);
                    buffer_.erase(cursor_, e - cursor_);
                    dirty_ = true;
                    break;
                }

                case Kind::KillToStart:
                {
                    const size_t s = lineStart(cursor_);
                    buffer_.erase(s, cursor_ - s);
                    cursor_ = s;
                    dirty_ = true;
                    break;
                }

                case Kind::DeleteWordBack:
                {
                    const size_t s = wordLeft(cursor_);
                    buffer_.erase(s, cursor_ - s);
                    cursor_ = s;
                    dirty_ = true;
                    break;
                }

                case Kind::Up:
                    if (lineStart(cursor_) > 0)
                        moveLine(-1);
                    else
                        recallHistory(true);
                    break;

                case Kind::Down:
                    if (lineEnd(cursor_) < buffer_.size())
                        moveLine(+1);
                    else
                        recallHistory(false);
                    break;

                case Kind::ClearScreen:
                    writeUi("\x1b[2J\x1b[H");
                    curRow_ = 0;
                    firstRender_ = true;
                    dirty_ = true;
                    break;

                case Kind::PasteStart:
                    inPaste_ = true;
                    break;

                case Kind::PasteEnd:
                    inPaste_ = false;
                    break;

                case Kind::CtrlC:
                    inPaste_ = false;
                    finishLine(false);
                    return EntryStatus::Cancelled;

                case Kind::EofOrDelete:
                    if (buffer_.empty())
                    {
                        writeUi("\r\n");
                        return EntryStatus::Eof;
                    }
                    deleteForward();
                    break;

                case Kind::EofOnly:
                    if (buffer_.empty())
                    {
                        writeUi("\r\n");
                        return EntryStatus::Eof;
                    }
                    break;

                case Kind::Hangup:
                    return EntryStatus::Eof;

                case Kind::Unknown:
                    break; // keys we don't use are swallowed whole, so their
                           // raw escape bytes can never leak into the buffer
                }
            }
        }

    private:
        static constexpr size_t kMaxHistory = 1000;

        Terminal &term_;
        std::string buffer_;
        size_t cursor_ = 0; // byte offset, always on a code point boundary
        int curRow_ = 0;    // row of the terminal cursor within the drawn entry
        int viewTop_ = 0;   // first screen row of the entry shown, when it is taller than the terminal
        bool firstRender_ = true;
        bool dirty_ = true;
        bool inPaste_ = false;
        std::vector<std::string> history_;
        size_t historyPos_ = 0;
        std::string draft_;

        enum class Kind
        {
            Char,
            Enter,
            ShiftEnter,
            Tab,
            Backspace,
            Delete,
            Left,
            Right,
            Up,
            Down,
            Home,
            End,
            WordLeft,
            WordRight,
            KillToEnd,
            KillToStart,
            DeleteWordBack,
            ClearScreen,
            CtrlC,
            EofOrDelete, // Ctrl+D
            EofOnly,     // Ctrl+Z (Windows)
            PasteStart,
            PasteEnd,
            Hangup,
            Unknown
        };
        struct Key
        {
            Kind kind;
            char32_t cp = 0;
        };

        // Ctrl+<letter> shortcuts, shared by every platform and key protocol.
        static Kind mapCtrl(int letter)
        {
            switch (letter)
            {
            case 'a': return Kind::Home;
            case 'b': return Kind::Left;
            case 'c': return Kind::CtrlC;
            case 'd': return Kind::EofOrDelete;
            case 'e': return Kind::End;
            case 'f': return Kind::Right;
            case 'h': return Kind::Backspace;
            case 'i': return Kind::Tab;
            case 'j': return Kind::Enter;
            case 'k': return Kind::KillToEnd;
            case 'l': return Kind::ClearScreen;
            case 'm': return Kind::Enter;
            case 'n': return Kind::Down;
            case 'p': return Kind::Up;
            case 'u': return Kind::KillToStart;
            case 'w': return Kind::DeleteWordBack;
            default: return Kind::Unknown;
            }
        }

        static Key charKey(char32_t cp)
        {
            if (cp < 0x20 || cp == 0x7F || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) || text::isRiskyCodePoint(cp))
                return Key{Kind::Unknown};
            return Key{Kind::Char, cp};
        }

        // ---- buffer helpers ------------------------------------------------

        static bool isBlank(const std::string &s)
        {
            for (char c : s)
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
                    return false;
            return true;
        }

        static void appendUtf8(std::string &out, char32_t cp)
        {
            if (cp < 0x80)
                out += static_cast<char>(cp);
            else if (cp < 0x800)
            {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        size_t prevBoundary(size_t pos) const
        {
            if (pos == 0)
                return 0;
            size_t p = pos - 1;
            while (p > 0 && (static_cast<unsigned char>(buffer_[p]) & 0xC0) == 0x80)
                --p;
            return p;
        }

        size_t nextBoundary(size_t pos) const
        {
            if (pos >= buffer_.size())
                return buffer_.size();
            size_t p = pos + 1;
            while (p < buffer_.size() && (static_cast<unsigned char>(buffer_[p]) & 0xC0) == 0x80)
                ++p;
            return p;
        }

        size_t lineStart(size_t pos) const
        {
            const size_t nl = pos == 0 ? std::string::npos : buffer_.rfind('\n', pos - 1);
            return nl == std::string::npos ? 0 : nl + 1;
        }

        size_t lineEnd(size_t pos) const
        {
            const size_t nl = buffer_.find('\n', pos);
            return nl == std::string::npos ? buffer_.size() : nl;
        }

        static bool isSpaceByte(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

        size_t wordLeft(size_t pos) const
        {
            while (pos > 0 && isSpaceByte(buffer_[pos - 1]))
                --pos;
            while (pos > 0 && !isSpaceByte(buffer_[pos - 1]))
                --pos;
            return pos;
        }

        size_t wordRight(size_t pos) const
        {
            const size_t n = buffer_.size();
            while (pos < n && isSpaceByte(buffer_[pos]))
                ++pos;
            while (pos < n && !isSpaceByte(buffer_[pos]))
                ++pos;
            return pos;
        }

        void insertText(const std::string &s)
        {
            if (buffer_.size() + s.size() > cuff::limits::kMaxSourceBytes)
                return;
            buffer_.insert(cursor_, s);
            cursor_ += s.size();
            dirty_ = true;
        }

        void insertCodepoint(char32_t cp)
        {
            std::string s;
            appendUtf8(s, cp);
            insertText(s);
        }

        void deleteForward()
        {
            if (cursor_ < buffer_.size())
            {
                buffer_.erase(cursor_, nextBoundary(cursor_) - cursor_);
                dirty_ = true;
            }
        }

        // Up/Down inside a multi-line entry: land on the same screen column on
        // the neighbouring line (a Hangul syllable is two columns wide), or at
        // that line's end if it is shorter.
        void moveLine(int dir)
        {
            int col = 0;
            for (size_t p = lineStart(cursor_); p < cursor_; p = nextBoundary(p))
            {
                char32_t cp;
                detail::decodeAt(buffer_, p, cp);
                col += cpWidth(cp);
            }

            size_t target, end;
            if (dir < 0)
            {
                end = lineStart(cursor_) - 1; // the '\n' that ends the previous line
                target = lineStart(end);
            }
            else
            {
                target = lineEnd(cursor_) + 1;
                end = lineEnd(target);
            }

            size_t p = target;
            int used = 0;
            while (p < end)
            {
                char32_t cp;
                detail::decodeAt(buffer_, p, cp);
                const int w = cpWidth(cp);
                if (used + w > col)
                    break;
                used += w;
                p = nextBoundary(p);
            }
            cursor_ = p;
            dirty_ = true;
        }

        void recallHistory(bool older)
        {
            if (older)
            {
                if (historyPos_ == 0)
                    return;
                if (historyPos_ == history_.size())
                    draft_ = buffer_;
                --historyPos_;
                buffer_ = history_[historyPos_];
            }
            else
            {
                if (historyPos_ >= history_.size())
                    return;
                ++historyPos_;
                buffer_ = (historyPos_ == history_.size()) ? draft_ : history_[historyPos_];
            }
            cursor_ = buffer_.size();
            dirty_ = true;
        }

        // ---- drawing ---------------------------------------------------------

        void writeUi(const std::string &s)
        {
            if (term_.ui() == Terminal::Ui::None)
                return;
            std::ostream &o = term_.uiStream();
            o.write(s.data(), static_cast<std::streamsize>(s.size()));
            o.flush();
        }

        // Draws the entry. If it is taller than the terminal, only the rows around
        // the cursor are drawn (plus a status line): the drawn area must always
        // fit on screen, because the cursor can't be moved above the top of the
        // screen, so anything that had scrolled off could never be erased and
        // would be left behind as a stale copy on every redraw.
        void render()
        {
            if (term_.ui() == Terminal::Ui::None)
                return;
            const int cols = std::max(4, term_.columns());
            const int termRows = std::max(4, term_.rows());
            const int rowsAvail = std::max(3, termRows - 1);

            const detail::RowsLayout lay =
                detail::layoutRows(buffer_, cursor_, cols, term_.paintUi("1;36", ">> "), term_.paintUi("2", ".. "));
            const int total = static_cast<int>(lay.rows.size());
            const bool tall = total > rowsAvail;
            const int capText = tall ? rowsAvail - 1 : total;

            int top = 0;
            if (tall)
            {
                top = viewTop_;
                if (lay.cursor.row < top)
                    top = lay.cursor.row;
                if (lay.cursor.row >= top + capText)
                    top = lay.cursor.row - capText + 1;
                top = std::max(0, std::min(top, total - capText));
            }
            viewTop_ = top;

            std::string f;
            if (!firstRender_)
            {
                f += '\r';
                const int up = std::min(curRow_, termRows - 1);
                if (up > 0)
                    f += "\x1b[" + std::to_string(up) + "A";
                f += "\x1b[J";
            }
            for (int i = 0; i < capText; ++i)
            {
                const detail::Row &rw = lay.rows[static_cast<size_t>(top + i)];
                f += rw.text;
                if (i + 1 < capText && !rw.wrapsToNext)
                    f += "\r\n";
            }

            int drawnRows = capText;
            if (tall)
            {
                std::string status = "[rows " + std::to_string(top + 1) + "-" + std::to_string(top + capText) + " of " +
                                     std::to_string(total) + "  - Up/Down scroll]";
                if (static_cast<int>(status.size()) > cols - 1)
                    status.resize(static_cast<size_t>(cols - 1));
                f += "\r\n" + term_.paintUi("2", status);
                drawnRows = capText + 1;
            }

            const int cursorRel = lay.cursor.row - top;
            if (tall || cursor_ != buffer_.size())
            {
                const int up = (drawnRows - 1) - cursorRel;
                if (up > 0)
                    f += "\x1b[" + std::to_string(up) + "A";
                f += '\r';
                if (lay.cursor.col > 0)
                    f += "\x1b[" + std::to_string(lay.cursor.col) + "C";
                curRow_ = cursorRel;
            }
            else
            {
                curRow_ = drawnRows - 1;
            }
            writeUi(f);
            firstRender_ = false;
        }

        // Leaves the entry on screen and puts the terminal cursor on a fresh
        // line below it, wherever the editing cursor was. `keep` = leave the
        // text visible (submit); a cancelled entry that was too tall to show in
        // full is simply erased.
        void finishLine(bool keep)
        {
            cursor_ = buffer_.size();
            const int cols = std::max(4, term_.columns());
            const int termRows = std::max(4, term_.rows());
            const int rowsAvail = std::max(3, termRows - 1);
            const detail::RowsLayout lay =
                detail::layoutRows(buffer_, cursor_, cols, term_.paintUi("1;36", ">> "), term_.paintUi("2", ".. "));
            if (static_cast<int>(lay.rows.size()) <= rowsAvail)
            {
                render();
                dirty_ = false;
                writeUi("\r\n");
                return;
            }
            // Taller than the screen: swap the scrolling view for the whole
            // entry, printed once, so the full text is in the terminal's history.
            std::string f = "\r";
            const int up = std::min(curRow_, termRows - 1);
            if (up > 0)
                f += "\x1b[" + std::to_string(up) + "A";
            f += "\x1b[J";
            if (keep)
            {
                for (size_t i = 0; i < lay.rows.size(); ++i)
                {
                    f += lay.rows[i].text;
                    if (i + 1 < lay.rows.size() && !lay.rows[i].wrapsToNext)
                        f += "\r\n";
                }
                f += "\r\n";
            }
            writeUi(f);
            dirty_ = false;
        }

        // ---- plain-line fallback --------------------------------------------

        // Used when the terminal can't be put in raw mode or can't do cursor
        // movement (TERM=dumb, old Windows consoles, no terminal to draw on):
        // ordinary line input, no Shift+Enter.
        EntryStatus readSimple(std::string &out)
        {
            for (;;)
            {
                if (term_.ui() != Terminal::Ui::None)
                    writeUi(term_.paintUi("1;36", ">> "));
                std::string line;
                if (!std::getline(std::cin, line))
                    return EntryStatus::Eof;
                std::string clean;
                for (size_t i = 0; i < line.size();)
                {
                    const unsigned char c = static_cast<unsigned char>(line[i]);
                    if (c == '\t')
                    {
                        clean += "    ";
                        ++i;
                    }
                    else if (c < 0x20 || c == 0x7F)
                    {
                        ++i;
                    }
                    else
                    {
                        char32_t cp = 0;
                        const size_t len = text::utf8Sequence(line, i, cp);
                        if (len == 0 || text::isRiskyCodePoint(cp))
                            ++i;
                        else
                        {
                            clean.append(line, i, len);
                            i += len;
                        }
                    }
                }
                if (isBlank(clean))
                    continue;
                out = clean;
                return EntryStatus::Submitted;
            }
        }

        bool canEditInPlace() const
        {
            return term_.inputIsTerminal() && term_.ui() != Terminal::Ui::None && term_.uiSupportsVt();
        }

        struct RawModeScope
        {
            LineEditor &owner;
            bool ok;
            explicit RawModeScope(LineEditor &o) : owner(o), ok(o.enableRawMode()) {}
            ~RawModeScope()
            {
                if (ok)
                    owner.disableRawMode();
            }
            RawModeScope(const RawModeScope &) = delete;
            RawModeScope &operator=(const RawModeScope &) = delete;
        };

        // ---- platform: keys --------------------------------------------------

#if defined(_WIN32)
        DWORD savedMode_ = 0;
        wchar_t pendingHigh_ = 0;
        bool lastWasCr_ = false; // the previous key was an Enter that carried a CR

        bool enableRawMode()
        {
            const HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
            if (h == INVALID_HANDLE_VALUE || h == nullptr || !GetConsoleMode(h, &savedMode_))
                return false;
            // No line editing, echo or processed-input by the console: we do
            // all of that ourselves, which is also what lets Ctrl+C arrive as
            // a key and Shift+Enter be told apart from Enter. VT input is
            // deliberately left OFF — with it on, arrow keys would arrive as
            // escape-sequence characters instead of virtual-key events.
            const DWORD raw = savedMode_ & ~(static_cast<DWORD>(ENABLE_LINE_INPUT) | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT |
                                             ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT);
            return SetConsoleMode(h, raw) != 0;
        }

        void disableRawMode() { SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), savedMode_); }

        static bool isModifierVk(WORD vk)
        {
            return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL ||
                   vk == VK_LWIN || vk == VK_RWIN;
        }

        // True if more real key presses are already queued (a paste, or
        // typing that outran us) — modifier-only presses and key releases
        // don't count.
        bool hasPendingKeyDown()
        {
            INPUT_RECORD recs[32];
            DWORD n = 0;
            if (!PeekConsoleInputW(GetStdHandle(STD_INPUT_HANDLE), recs, 32, &n))
                return false;
            for (DWORD i = 0; i < n; ++i)
            {
                if (recs[i].EventType != KEY_EVENT || !recs[i].Event.KeyEvent.bKeyDown)
                    continue;
                if (isModifierVk(recs[i].Event.KeyEvent.wVirtualKeyCode))
                    continue;
                return true;
            }
            return false;
        }

        bool inputPending() { return hasPendingKeyDown(); }

        Key nextKey()
        {
            const HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
            for (;;)
            {
                INPUT_RECORD rec;
                DWORD got = 0;
                if (!ReadConsoleInputW(h, &rec, 1, &got) || got == 0)
                    return Key{Kind::Hangup};
                if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown)
                    continue; // key releases, focus/menu events: nothing to do

                const KEY_EVENT_RECORD &ke = rec.Event.KeyEvent;
                const wchar_t wc = ke.uChar.UnicodeChar;
                const DWORD st = ke.dwControlKeyState;
                const bool shift = (st & SHIFT_PRESSED) != 0;
                const bool ctrl = (st & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
                const bool alt = (st & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
                const bool altGr = ctrl && alt && wc >= 0x20; // AltGr reports Ctrl+Alt but yields a real character
                const bool afterCr = lastWasCr_;
                lastWasCr_ = false;

                switch (ke.wVirtualKeyCode)
                {
                case VK_RETURN:
                    // A pasted CRLF can arrive as two Enter events ('\r' then
                    // '\n'); the second is the same line break, not another.
                    if (wc == L'\n' && afterCr)
                        continue;
                    lastWasCr_ = (wc == L'\r');
                    if (shift || (alt && !altGr))
                        return Key{Kind::ShiftEnter};
                    // Enter with more typing already queued behind it is a
                    // multi-line paste, not a request to run the first line.
                    if (hasPendingKeyDown())
                        return Key{Kind::ShiftEnter};
                    return Key{Kind::Enter};
                case VK_BACK: return Key{ctrl ? Kind::DeleteWordBack : Kind::Backspace};
                case VK_TAB: return Key{shift ? Kind::Unknown : Kind::Tab};
                case VK_LEFT: return Key{ctrl ? Kind::WordLeft : Kind::Left};
                case VK_RIGHT: return Key{ctrl ? Kind::WordRight : Kind::Right};
                case VK_UP: return Key{Kind::Up};
                case VK_DOWN: return Key{Kind::Down};
                case VK_HOME: return Key{Kind::Home};
                case VK_END: return Key{Kind::End};
                case VK_DELETE: return Key{Kind::Delete};
                case VK_ESCAPE: return Key{Kind::Unknown};
                default: break;
                }

                if (wc == 0)
                {
                    if (isModifierVk(ke.wVirtualKeyCode))
                        continue; // a bare Shift/Ctrl/Alt press
                    return Key{Kind::Unknown}; // function keys, etc.
                }
                if (wc < 0x20)
                {
                    if (wc == 0x1A)
                        return Key{Kind::EofOnly};
                    return Key{mapCtrl('a' + wc - 1)};
                }
                if (wc == 0x7F)
                    return Key{Kind::Unknown};

                if (wc >= 0xD800 && wc <= 0xDBFF)
                {
                    pendingHigh_ = wc;
                    return Key{Kind::Unknown};
                }
                if (wc >= 0xDC00 && wc <= 0xDFFF)
                {
                    if (pendingHigh_ != 0)
                    {
                        const char32_t cp = 0x10000u + ((static_cast<unsigned>(pendingHigh_) - 0xD800u) << 10) +
                                             (static_cast<unsigned>(wc) - 0xDC00u);
                        pendingHigh_ = 0;
                        return charKey(cp);
                    }
                    return Key{Kind::Unknown};
                }
                pendingHigh_ = 0;
                return charKey(static_cast<char32_t>(wc));
            }
        }
#else
        struct termios saved_ {};
        bool rawOn_ = false;
        std::deque<int> pending_; // bytes read ahead but not consumed yet
        bool kittyProbed_ = false;
        bool kittySupported_ = false;

        bool enableRawMode()
        {
            if (tcgetattr(0, &saved_) != 0)
                return false;
            g_savedTermios = saved_;
            struct termios raw = saved_;
            raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
            raw.c_iflag &= ~(IXON | ICRNL | INLCR | IGNCR | BRKINT | INPCK | ISTRIP);
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;
            g_rawActive.store(true);
            if (tcsetattr(0, TCSANOW, &raw) != 0)
            {
                g_rawActive.store(false);
                return false;
            }
            rawOn_ = true;
            pending_.clear();
            // Bracketed paste is a plain private mode every terminal ignores if
            // it doesn't know it. The Kitty keyboard protocol is only requested
            // after the terminal says it speaks it (probed once): a terminal
            // that doesn't could misread "CSI > 1 u" as something else.
            writeUi(kPasteOn);
            if (!kittyProbed_)
            {
                kittySupported_ = probeKitty();
                kittyProbed_ = true;
            }
            if (kittySupported_)
            {
                writeUi(kKittyPush);
                g_kittyActive.store(true);
            }
            return true;
        }

        void disableRawMode()
        {
            if (!rawOn_)
                return;
            if (g_kittyActive.exchange(false))
                writeUi(kKittyPop);
            writeUi(kPasteOff);
            tcsetattr(0, TCSANOW, &saved_);
            g_rawActive.store(false);
            rawOn_ = false;
        }

        // Asks "do you support the Kitty keyboard protocol?" (CSI ? u) followed
        // by a request every terminal answers (primary device attributes, CSI c)
        // as a sentinel: if the protocol answer shows up before the sentinel
        // answer, it is supported. A terminal that answers neither within the
        // timeout is treated as unsupported.
        bool probeKitty()
        {
            writeUi("\x1b[?u\x1b[c");
            bool kitty = false;
            std::deque<int> stash; // anything the user typed meanwhile
            auto done = [&](bool result)
            {
                pending_.insert(pending_.begin(), stash.begin(), stash.end());
                return result;
            };
            for (int guard = 0; guard < 256; ++guard)
            {
                int b = readByte(250);
                if (b < 0)
                    return done(kitty);
                if (b != 0x1B)
                {
                    stash.push_back(b);
                    continue;
                }
                const int b1 = readByte(100);
                if (b1 != '[')
                {
                    stash.push_back(0x1B);
                    if (b1 >= 0)
                        stash.push_back(b1);
                    continue;
                }
                std::string params;
                int fin = -1;
                for (int i = 0; i < 32; ++i)
                {
                    const int c = readByte(100);
                    if (c < 0)
                        return done(kitty);
                    if (c >= 0x40 && c <= 0x7E)
                    {
                        fin = c;
                        break;
                    }
                    params += static_cast<char>(c);
                }
                if (fin >= 0 && !params.empty() && params[0] == '?')
                {
                    if (fin == 'u')
                        kitty = true;
                    else if (fin == 'c')
                        return done(kitty);
                    continue;
                }
                // Not an answer: a key the user pressed (arrow etc.) — keep it.
                stash.push_back(0x1B);
                stash.push_back('[');
                for (char ch : params)
                    stash.push_back(static_cast<unsigned char>(ch));
                if (fin >= 0)
                    stash.push_back(fin);
            }
            return done(kitty);
        }

        // timeoutMs < 0 blocks; otherwise returns -1 if nothing arrives in time.
        int readByte(int timeoutMs)
        {
            if (!pending_.empty())
            {
                const int b = pending_.front();
                pending_.pop_front();
                return b;
            }
            for (;;)
            {
                if (timeoutMs >= 0)
                {
                    struct pollfd pfd;
                    pfd.fd = 0;
                    pfd.events = POLLIN;
                    pfd.revents = 0;
                    const int rc = poll(&pfd, 1, timeoutMs);
                    if (rc == 0)
                        return -1;
                    if (rc < 0)
                    {
                        if (errno == EINTR)
                            continue;
                        return -1;
                    }
                }
                unsigned char c;
                const ssize_t n = read(0, &c, 1);
                if (n == 1)
                    return c;
                if (n < 0 && errno == EINTR)
                    continue;
                return -2; // end of input / terminal gone
            }
        }

        bool inputPending()
        {
            if (!pending_.empty())
                return true;
            struct pollfd pfd;
            pfd.fd = 0;
            pfd.events = POLLIN;
            pfd.revents = 0;
            return poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN);
        }

        Key nextKey()
        {
            // While inside a bracketed paste, input is expected to keep
            // flowing; if it stops for a few seconds the end marker was lost
            // (connection hiccup), so leave paste mode rather than turning
            // every later Enter into a newline.
            const int c = readByte(inPaste_ ? 3000 : -1);
            if (c == -1 && inPaste_)
            {
                inPaste_ = false;
                return Key{Kind::Unknown};
            }
            if (c < 0)
                return Key{Kind::Hangup};

            if (inPaste_)
            {
                if (c == 0x03)
                    return Key{Kind::CtrlC}; // escape hatch if the terminal never sends the paste-end marker
                if (c == 0x1B)
                    return decodeEscape();
                if (c == '\r')
                {
                    const int n = readByte(5);
                    if (n >= 0 && n != '\n')
                        pending_.push_front(n); // a lone CR; CRLF collapses to one newline
                    return Key{Kind::ShiftEnter};
                }
                if (c == '\n')
                    return Key{Kind::ShiftEnter};
                if (c == '\t')
                    return Key{Kind::Tab};
                if (c < 0x20 || c == 0x7F)
                    return Key{Kind::Unknown}; // pasted control bytes are dropped
                return decodeUtf8(c);
            }

            if (c == 0x1B)
                return decodeEscape();
            if (c == '\r' || c == '\n')
                return Key{Kind::Enter};
            if (c == 0x7F)
                return Key{Kind::Backspace};
            if (c < 0x20)
                return Key{mapCtrl('a' + c - 1)};
            return decodeUtf8(c);
        }

        Key decodeUtf8(int lead)
        {
            if (lead < 0x80)
                return charKey(static_cast<char32_t>(lead));
            int extra;
            char32_t cp, minimum;
            if ((lead & 0xE0) == 0xC0)
                extra = 1, cp = lead & 0x1F, minimum = 0x80;
            else if ((lead & 0xF0) == 0xE0)
                extra = 2, cp = lead & 0x0F, minimum = 0x800;
            else if ((lead & 0xF8) == 0xF0)
                extra = 3, cp = lead & 0x07, minimum = 0x10000;
            else
                return Key{Kind::Unknown}; // stray continuation byte / invalid lead
            for (int i = 0; i < extra; ++i)
            {
                const int b = readByte(200);
                if (b < 0)
                    return Key{Kind::Unknown};
                if ((b & 0xC0) != 0x80)
                {
                    pending_.push_front(b); // not ours: it is the next key
                    return Key{Kind::Unknown};
                }
                cp = (cp << 6) | (static_cast<unsigned>(b) & 0x3F);
            }
            if (cp < minimum)
                return Key{Kind::Unknown}; // overlong encoding
            return charKey(cp);
        }

        // A key reported as (code, modifiers) — the shape both the Kitty
        // protocol (CSI code ; mods u) and modifyOtherKeys (CSI 27 ; mods ;
        // code ~) use. `mods` is the 0-based bitmask: shift 1, alt 2, ctrl 4.
        static Key keyFromCode(int code, int mods)
        {
            const bool shift = (mods & 1) != 0, alt = (mods & 2) != 0, ctrl = (mods & 4) != 0;
            const int other = mods & ~7; // super / hyper / meta
            if (code == 13)
                return Key{mods == 0 ? Kind::Enter : Kind::ShiftEnter};
            if (code == 9)
                return Key{mods == 0 ? Kind::Tab : Kind::Unknown};
            if (code == 127 || code == 8)
                return Key{(ctrl || alt) ? Kind::DeleteWordBack : Kind::Backspace};
            if (code == 27)
                return Key{Kind::Unknown};
            if (ctrl && !alt && !shift && other == 0 && code >= 'a' && code <= 'z')
                return Key{mapCtrl(code)};
            if (alt && !ctrl && !shift && other == 0)
            {
                if (code == 'b')
                    return Key{Kind::WordLeft};
                if (code == 'f')
                    return Key{Kind::WordRight};
                return Key{Kind::Unknown};
            }
            if (!ctrl && !alt && other == 0 && code >= 0x20)
                return charKey(static_cast<char32_t>(code));
            return Key{Kind::Unknown};
        }

        Key decodeEscape()
        {
            const int b = readByte(100);
            if (b < 0)
                return Key{Kind::Unknown}; // the Escape key on its own
            if (b == '[')
                return decodeCsi();
            if (b == 'O')
            {
                switch (readByte(100))
                {
                case 'A': return Key{Kind::Up};
                case 'B': return Key{Kind::Down};
                case 'C': return Key{Kind::Right};
                case 'D': return Key{Kind::Left};
                case 'H': return Key{Kind::Home};
                case 'F': return Key{Kind::End};
                default: return Key{Kind::Unknown};
                }
            }
            if (b == '\r' || b == '\n')
                return Key{Kind::ShiftEnter}; // Alt+Enter
            if (b == 'b')
                return Key{Kind::WordLeft};
            if (b == 'f')
                return Key{Kind::WordRight};
            if (b == 0x7F)
                return Key{Kind::DeleteWordBack};
            return Key{Kind::Unknown};
        }

        Key decodeCsi()
        {
            std::string params;
            int fin = -1;
            for (int guard = 0; guard < 64; ++guard)
            {
                const int c = readByte(100);
                if (c < 0)
                    return Key{Kind::Unknown};
                if (c >= 0x40 && c <= 0x7E)
                {
                    fin = c;
                    break;
                }
                params += static_cast<char>(c);
            }
            if (fin < 0)
                return Key{Kind::Unknown};

            // "1;5" / "13;2:1" / "27;2;13" -> numbers (sub-parameters after ':' ignored)
            std::vector<int> p;
            {
                size_t i = 0;
                while (i < params.size() && (params[i] == '<' || params[i] == '=' || params[i] == '>' || params[i] == '?'))
                    ++i;
                int cur = 0;
                bool inSub = false;
                for (; i <= params.size(); ++i)
                {
                    if (i == params.size() || params[i] == ';')
                    {
                        p.push_back(cur);
                        cur = 0;
                        inSub = false;
                    }
                    else if (params[i] == ':')
                        inSub = true;
                    else if (!inSub && params[i] >= '0' && params[i] <= '9' && cur < 1000000)
                        cur = cur * 10 + (params[i] - '0');
                }
            }
            const int p0 = p.size() > 0 ? p[0] : 0;
            const int p1 = p.size() > 1 ? p[1] : 0;
            const int p2 = p.size() > 2 ? p[2] : 0;
            const int mods = (p1 > 1 ? p1 - 1 : 0) & ~(64 | 128); // drop caps-lock / num-lock bits
            const bool wordMod = (mods & (2 | 4)) != 0;

            switch (fin)
            {
            case 'A': return Key{Kind::Up};
            case 'B': return Key{Kind::Down};
            case 'C': return Key{wordMod ? Kind::WordRight : Kind::Right};
            case 'D': return Key{wordMod ? Kind::WordLeft : Kind::Left};
            case 'H': return Key{Kind::Home};
            case 'F': return Key{Kind::End};
            case 'u': return keyFromCode(p0, mods);
            case '~':
                switch (p0)
                {
                case 1:
                case 7: return Key{Kind::Home};
                case 4:
                case 8: return Key{Kind::End};
                case 3: return Key{Kind::Delete};
                case 200: return Key{Kind::PasteStart};
                case 201: return Key{Kind::PasteEnd};
                case 27: return keyFromCode(p2, mods);
                default: return Key{Kind::Unknown};
                }
            default: return Key{Kind::Unknown};
            }
        }
#endif
    };

} // namespace cuff::cli
