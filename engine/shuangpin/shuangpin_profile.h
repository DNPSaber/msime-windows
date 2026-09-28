#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

struct ShuangpinProfile
{
    std::string name;
    std::unordered_map<std::string, std::string> initials;
    std::unordered_map<std::string, std::string> zero_initials;
    std::unordered_map<std::string, std::string> finals;
};

// Profiles have static storage duration and can safely be shared by sessions.
const ShuangpinProfile &GetXiaoheShuangpinProfile();
const ShuangpinProfile &GetZiranmaShuangpinProfile();
const ShuangpinProfile &GetShoudaoShuangpinProfile();
const ShuangpinProfile &GetMicrosoftShuangpinProfile();
const ShuangpinProfile &GetSogouShuangpinProfile();
const ShuangpinProfile &GetZiguangShuangpinProfile();
const ShuangpinProfile &GetZhinengAbcShuangpinProfile();
const ShuangpinProfile &GetGuobiaoShuangpinProfile();
const ShuangpinProfile &GetPinyinJiajiaShuangpinProfile();
const ShuangpinProfile &GetShuangpinProfile(std::string_view name);

// Microsoft, Sogou and Ziguang put the "ing" final on ';', so ';' is an input key
// in the second position of a syllable for those profiles.
bool ShuangpinProfileUsesSemicolonFinal(const ShuangpinProfile &profile);
