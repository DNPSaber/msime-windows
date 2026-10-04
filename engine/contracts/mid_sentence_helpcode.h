#pragma once

#include <cstddef>

// 双拼句中辅助码的输入形状（规则见 engine/core/syllable_helpcode.h）。
//
// TSF 在同步吃键阶段要先判断反引号是不是编码键，Server 随后按同一条规则决定收不收、引擎按它
// 解析输入串。三处必须一致：TSF 吃了而 Server 不收，两边的输入串从此分叉。TSF 不链接引擎、
// 没有音节表，所以这里只看形状，不判断前面的键是不是合法音节。
//
// 一段是反引号加至多两个字母：第一码（大小写都算）和紧跟其后的大写第二码。
//
// 触发键可以是反引号，也可以是分号（设置里多选）。无论按的是哪个，输入串里记的都是反引号：TSF 的
// 按键缓冲、Server 的 raw 和引擎解析的都是同一种串，分号在微软/搜狗/紫光双拼里还是 ing 韵母，
// 不能让它在串里有两种意思。分号只在 AcceptsMarkerAt 成立时才是触发键，那时这一节是偶数键，
// 而 ing 韵母只跟在奇数键之后，两者不会抢同一个位置。
namespace FanyImeMidSentenceHelpcode
{
inline constexpr char kMarker = '`';
inline constexpr char kSemicolonTrigger = ';';

template <typename Char> constexpr bool IsAsciiLetter(Char ch)
{
    return (ch >= Char('a') && ch <= Char('z')) || (ch >= Char('A') && ch <= Char('Z'));
}

template <typename Char> constexpr bool IsAsciiUpper(Char ch)
{
    return ch >= Char('A') && ch <= Char('Z');
}

// text[marker] 是反引号，返回这一段结束的位置。
template <typename Char> constexpr std::size_t BlockEnd(const Char *text, std::size_t size, std::size_t marker)
{
    std::size_t end = marker + 1;
    if (end < size && IsAsciiLetter(text[end]))
    {
        ++end;
        if (end < size && IsAsciiUpper(text[end]))
        {
            ++end;
        }
    }
    return end;
}

// 大写字母触发（设置里与反引号、分号并列多选）：完整双拼音节后面的大写字母自己开一段，相当于
// 「反引号 + 这个字母」，紧跟的大写字母仍是第二码。原串不改写——一个键换成两个字符会打乱 TSF 的
// 光标与退格——所以三处都按这里的规则解析带大写段的原串：
//
//   ulpbXihfa   ≡ ulpb`xihfa
//   ulpbXYih    ≡ ulpb`xYih
//
// 当前这一节已有的键数 chunk_length 是偶数且至少两个时，这个位置上的大写字母开一段。
template <typename Char> constexpr bool StartsUppercaseBlock(Char ch, std::size_t chunk_length)
{
    return IsAsciiUpper(ch) && chunk_length >= 2 && chunk_length % 2 == 0;
}

// text[first] 是大写段的第一码，返回这一段结束的位置。
template <typename Char> constexpr std::size_t UppercaseBlockEnd(const Char *text, std::size_t size, std::size_t first)
{
    std::size_t end = first + 1;
    if (end < size && IsAsciiUpper(text[end]))
    {
        ++end;
    }
    return end;
}

// 扫描 text[0, size)，每碰到一段（反引号段或大写段）调用 on_block(begin, end, codes_begin)，
// codes_begin 是第一码的位置；返回最后一节（最后一个 ' 或段之后）的起点。
template <typename Char, typename OnBlock>
constexpr std::size_t ScanBlocks(const Char *text, std::size_t size, bool uppercase_trigger, OnBlock &&on_block)
{
    std::size_t chunk_start = 0;
    std::size_t index = 0;
    while (index < size)
    {
        if (text[index] == Char('\''))
        {
            chunk_start = ++index;
        }
        else if (text[index] == Char(kMarker))
        {
            const std::size_t end = BlockEnd(text, size, index);
            on_block(index, end, index + 1);
            chunk_start = index = end;
        }
        else if (uppercase_trigger && StartsUppercaseBlock(text[index], index - chunk_start))
        {
            const std::size_t end = UppercaseBlockEnd(text, size, index);
            on_block(index, end, index);
            chunk_start = index = end;
        }
        else
        {
            ++index;
        }
    }
    return chunk_start;
}

template <typename Char>
constexpr std::size_t ChunkStart(const Char *text, std::size_t size, bool uppercase_trigger = false)
{
    return ScanBlocks(text, size, uppercase_trigger, [](std::size_t, std::size_t, std::size_t) {});
}

// text[0, size) 末尾能否接一个反引号：当前这一节（最后一个 ' 或段之后）是偶数个、
// 至少两个键，也就是光标前恰好是一个个完整的双拼两键音节。
template <typename Char>
constexpr bool AcceptsMarker(const Char *text, std::size_t size, bool uppercase_trigger = false)
{
    const std::size_t chunk_length = size - ChunkStart(text, size, uppercase_trigger);
    return chunk_length >= 2 && chunk_length % 2 == 0;
}

// 光标停在 text[caret] 时能否插入一个反引号：光标前按 AcceptsMarker 判断，后面的部分不看——
// 光标移回句中补辅助码时，它后面还有没敲完的音节。光标后紧跟的已经是这个音节的段时不收，
// 一个音节上叠两段只会让后一段把前一段盖掉；大写触发开着时，光标后的大写字母正好开这个音节的段。
template <typename Char>
constexpr bool AcceptsMarkerAt(const Char *text, std::size_t size, std::size_t caret, bool uppercase_trigger = false)
{
    return caret <= size && AcceptsMarker(text, caret, uppercase_trigger) &&
           (caret == size || (text[caret] != Char(kMarker) && !(uppercase_trigger && IsAsciiUpper(text[caret]))));
}

// 光标停在 text[caret] 时大写字母能否作为第二码：光标前恰好是一段只有第一码的段（反引号 + 一码，
// 或大写触发的一码）。
template <typename Char>
constexpr bool AcceptsSecondCodeAt(const Char *text, std::size_t size, std::size_t caret,
                                   bool uppercase_trigger = false)
{
    if (caret > size)
    {
        return false;
    }
    bool open = false;
    ScanBlocks(text, caret, uppercase_trigger, [&](std::size_t, std::size_t end, std::size_t codes_begin) {
        open = end == caret && end - codes_begin == 1;
    });
    return open && IsAsciiLetter(text[caret - 1]);
}

// 微软/搜狗/紫光双拼的 ; 是 ing 韵母，光标前这一节是奇数键时它是编码键。大写触发开着时，大写段不算
// 这一节的键（ulpbXi; 里 i; 才是一个音节）；关着时保持原来只看最后一个 ' 之后的规则。
template <typename Char>
constexpr bool AcceptsSemicolonFinalAt(const Char *text, std::size_t size, std::size_t caret, bool uppercase_trigger)
{
    if (caret > size)
    {
        return false;
    }
    std::size_t chunk_start = 0;
    if (uppercase_trigger)
    {
        chunk_start = ChunkStart(text, caret, true);
    }
    else
    {
        for (std::size_t index = caret; index > 0; --index)
        {
            if (text[index - 1] == Char('\''))
            {
                chunk_start = index;
                break;
            }
        }
    }
    return (caret - chunk_start) % 2 == 1;
}
} // namespace FanyImeMidSentenceHelpcode
