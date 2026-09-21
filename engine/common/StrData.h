#pragma once

#include "Utf8.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace cuff
{

    class StrData
    {
    public:
        explicit StrData(std::string s) : text_(std::move(s)) {}

        const std::string &text() const { return text_; }

        bool ascii() const
        {
            if (asciiState_ < 0)
                asciiState_ = scanAscii() ? 1 : 0;
            return asciiState_ == 1;
        }

        size_t codepoints() const
        {
            if (ascii())
                return text_.size();
            if (cpCount_ == kUnknown)
                cpCount_ = utf8::length(text_);
            return cpCount_;
        }

        // Byte offset of the codepoint at 0-based index `cp` (cp == codepoints() gives size()).
        size_t offsetOf(size_t cp) const
        {
            if (ascii())
                return cp < text_.size() ? cp : text_.size();
            size_t idx = 0, off = 0;
            if (cursorCp_ <= cp)
            {
                idx = cursorCp_;
                off = cursorByte_;
            }
            const size_t n = text_.size();
            while (idx < cp && off < n)
            {
                off += utf8::seqLen(static_cast<unsigned char>(text_[off]));
                if (off > n)
                    off = n;
                ++idx;
            }
            cursorCp_ = idx;
            cursorByte_ = off;
            return off;
        }

    private:
        static constexpr size_t kUnknown = static_cast<size_t>(-1);

        std::string text_;
        mutable int8_t asciiState_ = -1;
        mutable size_t cpCount_ = kUnknown;
        mutable size_t cursorCp_ = 0;
        mutable size_t cursorByte_ = 0;

        bool scanAscii() const
        {
            const char *p = text_.data();
            size_t n = text_.size(), i = 0;
            for (; i + 8 <= n; i += 8)
            {
                uint64_t w;
                std::memcpy(&w, p + i, 8);
                if (w & 0x8080808080808080ULL)
                    return false;
            }
            for (; i < n; ++i)
                if (static_cast<unsigned char>(p[i]) & 0x80)
                    return false;
            return true;
        }
    };

} // namespace cuff
