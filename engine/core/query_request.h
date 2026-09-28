#pragma once

#include "key_event.h"
#include "fuzzy_pinyin_options.h"
#include "scheme_type.h"
#include "sentence_association_options.h"
#include <string>
#include <vector>

struct KeyStroke
{
    ImeKeyCode vk = 0;
    ImeModifierMask modifiers_down = 0;
    ImeCharacter wch = 0;
};

struct QueryRequest
{
    SchemeType scheme = SchemeType::Quanpin;
    std::string raw_input;
    std::string raw_input_with_cases;
    std::string normalized_input;
    std::string raw_segmentation;
    std::string normalized_segmentation;
    std::string segmentation;
    bool enable_shuangpin_helpcode = false;
    bool enable_quanpin_helpcode = false;
    // Autocorrection is type-gated (bit0 transposition, bit1 neighbor in the session-level
    // mask); both default off, so a fresh install never rewrites the user's spelling.
    bool enable_quanpin_autocorrect_transposition = false;
    bool enable_quanpin_autocorrect_neighbor = false;
    std::vector<KeyStroke> key_strokes;
    metasequoia::FuzzyPinyinOptions fuzzy_pinyin;
    // 整句候选来源与去重补位选项，默认全关。
    SentenceAssociationOptions sentence_association;
    // 给神经重排看的前文：本会话最近上屏的文本。空着也能重排，只是模型看不到语境，「shanghai」
    // 到底是上海还是伤害就只能靠词格自己的静态分。词典层只取末尾若干字（RerankOptions::
    // context_chars），所以这里给多了也无妨。
    std::string rescoring_context;
    // Wubi wildcard mode and the code in hand holds a z: the provider matches the code as a
    // pattern (z = one arbitrary letter) instead of a prefix range. Never set for other schemes.
    // With mixed input also on, the same z is offered to pinyin as a plain letter and those
    // candidates are placed ahead of the wildcard rows (see ImeSession::refresh_candidates).
    bool wubi_z_wildcard = false;
    bool valid = false;
};
