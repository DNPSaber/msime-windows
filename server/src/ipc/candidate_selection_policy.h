#pragma once

#include "engine/core/word_item.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace FanyImeIpc
{
// Cloud candidates already represent a complete result for the current
// query.  They must not turn a shorter cloud query into word-creation mode.
constexpr bool ShouldEnterCreatingWord(CandidateSource source, bool continues_composition) noexcept
{
    return continues_composition && source != CandidateSource::CloudSuggestion;
}

// 整句落库的长度上限，对标词库里短语自身的上限
// （quanpin::WordLatticeOptions::max_phrase_syllables）。再长的整句只是这一次输入
// 的产物，落库除了撑大用户词库没有别的作用。
constexpr size_t kMaxLearnedSentenceSyllables = 7;

inline size_t CountCanonicalSyllables(const std::string &canonical_pinyin) noexcept
{
    if (canonical_pinyin.empty())
    {
        return 0;
    }
    return 1 + static_cast<size_t>(std::count(canonical_pinyin.begin(), canonical_pinyin.end(), '\''));
}

// 猜出来的整句来源：词格 Generated、Google 解码器 Fallback，以及神经整句
// NeuralDesktop / NeuralKeyboard。它们都不是词库里已有的行，落库/学习判定同类处理。
constexpr bool IsGuessedSentenceSource(CandidateSource source) noexcept
{
    return source == CandidateSource::Generated || source == CandidateSource::Fallback ||
           source == CandidateSource::NeuralDesktop || source == CandidateSource::NeuralKeyboard;
}

// 整句候选独立上屏——不接在造词前缀后面、自己就是整条输入——时是否该落库。该落：整句是
// 猜出来的，词库里没有它那一行，调频改的是已有的行，改不到它。不落库的话用户选多少次，
// 下次它仍然要靠猜，也仍然排在词库里那条同音短语后面。
inline bool ShouldStoreStandaloneSentence(CandidateSource source,
                                          const std::string &candidate_canonical_pinyin) noexcept
{
    if (!IsGuessedSentenceSource(source))
    {
        return false;
    }
    const size_t syllables = CountCanonicalSyllables(candidate_canonical_pinyin);
    return syllables > 0 && syllables <= kMaxLearnedSentenceSyllables;
}

// Special candidates commit through an early return in ProcessSelectionKey,
// before the creating-word completion block that normally persists a composed
// phrase.  A lattice whole-sentence candidate therefore has to be stored at
// that early return instead, in both shapes it can take:
//   - 它接在造词前缀后面、结束一段造词时，前后两段都要有 canonical quanpin，否则
//     拼不出完整读音。这一支的长度由用户一段段选出来，维持原样不设上限；
//   - 它自己就是整条输入时，按 ShouldStoreStandaloneSentence 判定。
inline bool ShouldStoreEarlyReturnPhrase(CandidateSource source, bool creating_word_active,
                                         const std::string &prefix_canonical_pinyin,
                                         const std::string &candidate_canonical_pinyin) noexcept
{
    // 与词格 Generated 同一支：神经整句同样带 canonical quanpin，可以结束一段造词或独立
    // 上屏。Google Fallback 沿用旧语义仍不走这条造词落库路径。
    const bool early_return_source = source == CandidateSource::Generated || source == CandidateSource::NeuralDesktop ||
                                     source == CandidateSource::NeuralKeyboard;
    if (!early_return_source || candidate_canonical_pinyin.empty())
    {
        return false;
    }
    if (!creating_word_active)
    {
        return ShouldStoreStandaloneSentence(source, candidate_canonical_pinyin);
    }
    return !prefix_canonical_pinyin.empty();
}

// 异步候选的槽位按普通候选数，快捷短语不占槽位：越过 count 个非快捷短语候选后，再越过
// 紧跟着的整组快捷短语，所以同一位置上快捷短语排在异步候选前面，组也不会被拆开。
inline size_t IndexAfterLocalCandidates(const std::vector<WordItem> &items, size_t count)
{
    size_t index = 0;
    for (size_t counted = 0; index < items.size() && counted < count; ++index)
    {
        if (items[index].source != CandidateSource::QuickPhrase)
            ++counted;
    }
    while (index < items.size() && items[index].source == CandidateSource::QuickPhrase)
        ++index;
    return index;
}

// Keep asynchronous mixed-input candidates in stable priority slots regardless
// of the order in which their workers finish. English keeps its legacy slotting
// (promoted ahead of AI unless a cloud result forces it behind cloud+AI), and
// emoji/kaomoji are placed one slot after the last of cloud/AI/English:
//   no cloud:         Chinese, English, AI, emoji, kaomoji
//   cloud:            Chinese, cloud, AI, English, emoji, kaomoji
//   cloud only:       Chinese, cloud, English, emoji, kaomoji
//   base:             Chinese, English, emoji, kaomoji
// Explicit English ranking choices are reapplied after this default ordering.
inline void NormalizeMixedCandidateOrder(std::vector<WordItem> &items, size_t local_prefix_slots = 1)
{
    std::vector<WordItem> local_candidates;
    std::vector<WordItem> english_candidates;
    std::vector<WordItem> emoji_candidates;
    std::vector<WordItem> kaomoji_candidates;
    std::optional<WordItem> cloud_candidate;
    std::optional<WordItem> ai_candidate;
    local_candidates.reserve(items.size());

    for (auto &item : items)
    {
        switch (item.source)
        {
        case CandidateSource::CloudSuggestion:
            if (!cloud_candidate)
                cloud_candidate = std::move(item);
            break;
        case CandidateSource::AiSuggestion:
            if (!ai_candidate)
                ai_candidate = std::move(item);
            break;
        case CandidateSource::EnglishDictionary:
            english_candidates.push_back(std::move(item));
            break;
        case CandidateSource::Emoji:
            emoji_candidates.push_back(std::move(item));
            break;
        case CandidateSource::Kaomoji:
            kaomoji_candidates.push_back(std::move(item));
            break;
        default:
            local_candidates.push_back(std::move(item));
            break;
        }
    }

    items = std::move(local_candidates);
    auto insert_at = [&](size_t index, WordItem candidate) {
        const auto offset = static_cast<std::ptrdiff_t>((std::min)(index, items.size()));
        items.insert(items.begin() + offset, std::move(candidate));
    };

    size_t slot = IndexAfterLocalCandidates(items, local_prefix_slots);
    if (cloud_candidate)
    {
        insert_at(slot++, std::move(*cloud_candidate));
        if (ai_candidate)
            insert_at(slot++, std::move(*ai_candidate));
    }
    if (!english_candidates.empty())
    {
        insert_at(slot++, std::move(english_candidates.front()));
        english_candidates.erase(english_candidates.begin());
    }
    if (!cloud_candidate && ai_candidate)
        insert_at(slot++, std::move(*ai_candidate));
    if (!emoji_candidates.empty())
    {
        insert_at(slot++, std::move(emoji_candidates.front()));
        emoji_candidates.erase(emoji_candidates.begin());
    }
    if (!kaomoji_candidates.empty())
    {
        insert_at(slot++, std::move(kaomoji_candidates.front()));
        kaomoji_candidates.erase(kaomoji_candidates.begin());
    }

    for (auto &candidate : english_candidates)
        items.push_back(std::move(candidate));
    for (auto &candidate : emoji_candidates)
        items.push_back(std::move(candidate));
    for (auto &candidate : kaomoji_candidates)
        items.push_back(std::move(candidate));

    // 快捷短语的权重是另一套量级，不参与英文提升的比较。
    const auto ranked = [](const WordItem &item) { return item.source != CandidateSource::QuickPhrase; };
    auto promoted_english = items.end();
    for (auto candidate = items.begin(); candidate != items.end(); ++candidate)
    {
        if (ranked(*candidate) && (promoted_english == items.end() || promoted_english->weight < candidate->weight))
            promoted_english = candidate;
    }
    if (promoted_english != items.end() && promoted_english->source == CandidateSource::EnglishDictionary &&
        promoted_english->fixed_position == 0 && std::count_if(items.begin(), items.end(), [&](const WordItem &item) {
                                                     return ranked(item) && item.weight == promoted_english->weight;
                                                 }) == 1)
    {
        WordItem candidate = std::move(*promoted_english);
        items.erase(promoted_english);
        insert_at(IndexAfterLocalCandidates(items, 0), std::move(candidate));
    }

    std::vector<WordItem> fixed_english_candidates;
    for (auto candidate = items.begin(); candidate != items.end();)
    {
        if (candidate->source == CandidateSource::EnglishDictionary && candidate->fixed_position > 0)
        {
            fixed_english_candidates.push_back(std::move(*candidate));
            candidate = items.erase(candidate);
        }
        else
        {
            ++candidate;
        }
    }
    std::stable_sort(
        fixed_english_candidates.begin(), fixed_english_candidates.end(),
        [](const WordItem &left, const WordItem &right) { return left.fixed_position < right.fixed_position; });
    for (auto &candidate : fixed_english_candidates)
        insert_at(static_cast<size_t>(candidate.fixed_position - 1), std::move(candidate));
}
} // namespace FanyImeIpc
