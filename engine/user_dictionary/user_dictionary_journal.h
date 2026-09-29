#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>
#include "../core/word_item.h"

namespace user_dictionary
{
enum class DictionaryKind
{
    Pinyin,
    Wubi,
    QuickPhrase,
    English,
};

std::string default_user_db_path();
// Compatibility no-op; journal connections are operation-scoped.
void close_default_user_database();

bool record_upsert(const std::string &user_db_path, DictionaryKind kind, const std::string &key,
                   const std::string &value, std::int64_t weight, const std::string &display = {});
bool record_user_insert(const std::string &user_db_path, DictionaryKind kind, const std::string &key,
                        const std::string &value, std::int64_t weight, const std::string &display = {});
bool record_delete(const std::string &user_db_path, DictionaryKind kind, const std::string &key,
                   const std::string &value);
bool is_user_inserted(const std::string &user_db_path, DictionaryKind kind, const std::string &key,
                      const std::string &value);
bool ensure_user_database(const std::string &user_db_path);
bool record_pinyin_upsert_from_database(const std::string &main_db_path, const std::string &key,
                                        const std::string &value,
                                        const std::string &user_db_path = default_user_db_path());
// Raise the selected wubi candidate to the top of its own code group (max weight + 1, same
// semantics as the quanpin side) and persist the new weight plus a journal upsert in one
// attached-database transaction. Existing rows only: a missing (key,value) pair returns false
// without inserting. Row-missing behaviour matches update_wubi_weight.
bool bump_wubi_weight(const std::string &main_db_path, const std::string &user_db_path, const std::string &key,
                      const std::string &value);

// 快捷短语混排的调频状态。快捷短语与拼音候选的权重不在同一个量纲上，不能互相比较，所以
// 按编码记一个槽位：同码快捷短语组前面排几个普通候选（见
// metasequoia::local_modes::counts_toward_quick_phrase_slot）。没有记录即 0，组在首位。
int quick_phrase_slot(const std::string &user_db_path, const std::string &code);
// 用户选中了组里的一条快捷短语。累计到 trigger_count 次后按调频模式把组往前挪（槽位按
// ranking_target 缩小），选中的不是组内第一条时再把它的权重升到组内最高 + 1 并记日志。
// mode 为 "disabled" 时什么都不做。
bool learn_quick_phrase_selection(const std::string &main_db_path, const std::string &user_db_path,
                                  const std::string &code, const std::string &value, bool first_in_group,
                                  const std::string &mode, int linear_step, int trigger_count);
// 用户越过快捷短语组选了排在组后面的普通候选，ordinary_rank 是它在普通候选里的名次。
// 把整组当成它前面的一个位置，它按调频模式的目标位置越过这一组时，累计到 trigger_count
// 次后槽位 + 1，最多到 max_slot（首页最后一位）。
bool learn_quick_phrase_bypass(const std::string &user_db_path, const std::string &code, int ordinary_rank,
                               const std::string &mode, int linear_step, int trigger_count, int max_slot);

struct ReplayResult
{
    int applied = 0;
    int skipped = 0;
    int failed = 0;
    std::string error;
};

ReplayResult replay(const std::string &user_db_path, const std::string &main_db_path,
                    const std::string &english_db_path);

bool adjust_candidate_ranking(const std::string &main_db_path, const std::string &user_db_path,
                              const std::string &context_key, const std::vector<WordItem> &ordered_candidates,
                              const std::string &entry_key, const std::string &value, const std::string &mode,
                              int linear_step, int trigger_count, bool force_top, bool *ranking_changed = nullptr,
                              DictionaryKind kind = DictionaryKind::Pinyin);
bool adjust_english_candidate_ranking(const std::string &english_db_path, const std::string &user_db_path,
                                      const std::string &context_key, const std::vector<WordItem> &ordered_candidates,
                                      const std::string &entry_key, const std::string &value, const std::string &mode,
                                      int linear_step, int trigger_count, bool force_top,
                                      bool *ranking_changed = nullptr);
// Delete an exact dictionary key and persist its tombstone in one attached-database
// transaction. Supports Pinyin, Wubi and English; false preserves candidate rows and journal operations
// on statement/commit failures (not a cross-file power-loss guarantee in WAL mode).
bool delete_dictionary_candidate(const std::string &dictionary_db_path, const std::string &user_db_path,
                                 DictionaryKind kind, const std::string &entry_key, const std::string &value);
bool delete_english_candidate(const std::string &english_db_path, const std::string &user_db_path,
                              const std::string &entry_key, const std::string &value);
bool learn_entered_english_word(const std::string &english_db_path, const std::string &user_db_path,
                                const std::string &display, std::int64_t weight = 10);
bool set_fixed_position(const std::string &user_db_path, const std::string &context_key, const std::string &entry_key,
                        const std::string &value, int position);
bool clear_fixed_position(const std::string &user_db_path, const std::string &context_key, const std::string &entry_key,
                          const std::string &value);
bool is_fixed(const std::string &user_db_path, const std::string &context_key, const std::string &entry_key,
              const std::string &value);
// Cloud/AI suggestions are normally hoisted back to their fixed slots (cloud at
// index 1, AI at index 2). Set keep_dynamic_candidate_positions when the caller
// already decided where they belong, e.g. after helpcode filtering.
void apply_fixed_positions(
    const std::string &user_db_path, const std::string &context_key, std::vector<WordItem> &candidates,
    bool include_missing,
    const std::function<std::optional<WordItem>(const std::string &, const std::string &)> &find_candidate = {},
    bool keep_dynamic_candidate_positions = false);
} // namespace user_dictionary
