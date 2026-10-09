#include "ipc/candidate_text_policy.h"
#include "tests/includes/test_framework.h"

TEST_CASE(candidate_text_policy_extracts_first_and_last_han_character)
{
    const std::string candidate =
        "\xE4\xB8\xAD\xE5\x8D\x8E\xE4\xBA\xBA\xE6\xB0\x91\xE5\x85\xB1\xE5\x92\x8C\xE5\x9B\xBD";
    const auto first = FanyImeIpc::ExtractHanCharacter(candidate, FanyImeIpc::HanCharacterEdge::First);
    const auto last = FanyImeIpc::ExtractHanCharacter(candidate, FanyImeIpc::HanCharacterEdge::Last);
    REQUIRE(first.has_value());
    REQUIRE(last.has_value());
    REQUIRE_EQ(*first, std::string("\xE4\xB8\xAD"));
    REQUIRE_EQ(*last, std::string("\xE5\x9B\xBD"));
}

TEST_CASE(candidate_text_policy_skips_non_han_characters)
{
    const std::string candidate = "C\xE8\xAF\xAD\xE8\xA8\x80 2";
    const auto first = FanyImeIpc::ExtractHanCharacter(candidate, FanyImeIpc::HanCharacterEdge::First);
    const auto last = FanyImeIpc::ExtractHanCharacter(candidate, FanyImeIpc::HanCharacterEdge::Last);
    REQUIRE(first.has_value());
    REQUIRE(last.has_value());
    REQUIRE_EQ(*first, std::string("\xE8\xAF\xAD"));
    REQUIRE_EQ(*last, std::string("\xE8\xA8\x80"));
    REQUIRE(!FanyImeIpc::ExtractHanCharacter("GitHub", FanyImeIpc::HanCharacterEdge::First).has_value());
}

TEST_CASE(candidate_text_policy_supports_non_bmp_han_characters)
{
    const std::string candidate = "\xF0\xA0\x80\x80\xE6\x96\xB9\xE6\xA1\x88\xF0\xA0\xAE\xB7";
    const auto first = FanyImeIpc::ExtractHanCharacter(candidate, FanyImeIpc::HanCharacterEdge::First);
    const auto last = FanyImeIpc::ExtractHanCharacter(candidate, FanyImeIpc::HanCharacterEdge::Last);
    REQUIRE(first.has_value());
    REQUIRE(last.has_value());
    REQUIRE_EQ(*first, std::string("\xF0\xA0\x80\x80"));
    REQUIRE_EQ(*last, std::string("\xF0\xA0\xAE\xB7"));
}

TEST_CASE(candidate_text_policy_uses_the_highlighted_candidate)
{
    const std::vector<std::wstring> page_words = {L"first", L"highlighted", L"third"};
    REQUIRE_EQ(FanyImeIpc::HighlightedCandidateText(page_words, 1), std::wstring(L"highlighted"));
    REQUIRE(FanyImeIpc::HighlightedCandidateText(page_words, -1).empty());
    REQUIRE(FanyImeIpc::HighlightedCandidateText(page_words, 3).empty());
}

TEST_CASE(candidate_selection_in_dedicated_english_mode_adds_one_ascii_space)
{
    REQUIRE_EQ(FanyImeIpc::CandidateSelectionText(L"hello", true), std::wstring(L"hello "));
    REQUIRE_EQ(FanyImeIpc::CandidateSelectionText(L"GitHub", true), std::wstring(L"GitHub "));
}

TEST_CASE(candidate_selection_outside_dedicated_english_mode_keeps_original_text)
{
    REQUIRE_EQ(FanyImeIpc::CandidateSelectionText(L"\u4F60\u597D", false), std::wstring(L"\u4F60\u597D"));
    // Mixed English candidates and temporary Y-mode candidates do not enable dedicated mode.
    REQUIRE_EQ(FanyImeIpc::CandidateSelectionText(L"hello", false), std::wstring(L"hello"));
    REQUIRE(FanyImeIpc::CandidateSelectionText(L"", true).empty());
}

TEST_CASE(english_candidate_selection_leaves_page_and_punctuation_commit_text_unchanged)
{
    const std::vector<std::wstring> page_words = {L"hello", L"Help"};
    REQUIRE_EQ(FanyImeIpc::CandidateSelectionText(FanyImeIpc::HighlightedCandidateText(page_words, 1), true),
               std::wstring(L"Help "));
    // The punctuation path reads the displayed word, then appends the punctuation at the TSF client.
    REQUIRE_EQ(FanyImeIpc::HighlightedCandidateText(page_words, 1) + L",", std::wstring(L"Help,"));
    REQUIRE_EQ(page_words[1], std::wstring(L"Help"));
}
