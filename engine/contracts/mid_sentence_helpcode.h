#pragma once

#include <cstddef>

// 双拼句中辅助码的输入形状（规则见 engine/core/syllable_helpcode.h）。
//
// TSF 在同步吃键阶段要先判断反引号是不是编码键，Server 随后按同一条规则决定收不收、引擎按它
// 解析输入串。三处必须一致：TSF 吃了而 Server 不收，两边的输入串从此分叉。TSF 不链接引擎、
// 没有音节表，所以这里只看形状，不判断前面的键是不是合法音节。
//
// 一段是反引号加至多两个字母：第一码（大小写都算）和紧跟其后的大写第二码。
namespace FanyImeMidSentenceHelpcode
{
inline constexpr char kMarker = '`';

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

// text[0, size) 末尾能否接一个反引号：当前这一节（最后一个 ' 或反引号段之后）是偶数个、
// 至少两个键，也就是光标前恰好是一个个完整的双拼两键音节。
template <typename Char> constexpr bool AcceptsMarker(const Char *text, std::size_t size)
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
            chunk_start = index = BlockEnd(text, size, index);
        }
        else
        {
            ++index;
        }
    }
    const std::size_t chunk_length = size - chunk_start;
    return chunk_length >= 2 && chunk_length % 2 == 0;
}
} // namespace FanyImeMidSentenceHelpcode
