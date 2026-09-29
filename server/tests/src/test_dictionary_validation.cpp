#include "settings/dictionary_manager.h"
#include "settings/dictionary_validation.h"
#include "tests/includes/test_framework.h"

#include <windows.h>

#include <filesystem>
#include <string>

TEST_CASE(DictionaryFullPinyinValidationRejectsAbbreviatedSyllables)
{
    quanpin::Segments segments;
    std::string normalized;

    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("nihao", segments, normalized));
    REQUIRE_EQ(normalized, std::string("ni'hao"));
    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("ni hao", segments, normalized));
    REQUIRE_EQ(normalized, std::string("ni'hao"));
    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("xi'an", segments, normalized));
    REQUIRE_EQ(normalized, std::string("xi'an"));
    REQUIRE(!SettingsDictionary::Validation::NormalizeFullPinyin("nh", segments, normalized));
    REQUIRE(!SettingsDictionary::Validation::NormalizeFullPinyin("ni'h", segments, normalized));
    REQUIRE(!SettingsDictionary::Validation::NormalizeFullPinyin("ni'", segments, normalized));
}

TEST_CASE(DictionaryFullPinyinPicksSegmentationMatchingSyllableCount)
{
    quanpin::Segments segments;
    std::string normalized;

    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("xian", segments, normalized));
    REQUIRE_EQ(normalized, std::string("xian"));
    REQUIRE_EQ(segments.size(), static_cast<size_t>(1));

    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("xian", segments, normalized, 2));
    REQUIRE_EQ(normalized, std::string("xi'an"));
    REQUIRE_EQ(segments.size(), static_cast<size_t>(2));

    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("a'a'a'a'a'a'a'a", segments, normalized, 8));
    REQUIRE_EQ(normalized, std::string("a'a'a'a'a'a'a'a"));
    REQUIRE_EQ(segments.size(), static_cast<size_t>(8));

    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("abalatiyayunhai", segments, normalized, 7));
    REQUIRE_EQ(normalized, std::string("a'ba'la'ti'ya'yun'hai"));
    REQUIRE_EQ(segments.size(), static_cast<size_t>(7));
}

TEST_CASE(QuickPhraseValidationMatchesNamedPipeWcharCapacity)
{
    REQUIRE(SettingsDictionary::Validation::QuickPhraseFitsNamedPipe(std::string(199, 'a')));
    REQUIRE(!SettingsDictionary::Validation::QuickPhraseFitsNamedPipe(std::string(200, 'a')));

    const std::string emoji = "\xF0\x9F\x98\x80";
    REQUIRE(SettingsDictionary::Validation::QuickPhraseFitsNamedPipe(std::string(197, 'a') + emoji));
    REQUIRE(!SettingsDictionary::Validation::QuickPhraseFitsNamedPipe(std::string(198, 'a') + emoji));
}

TEST_CASE(CodedDictionaryImportRequiresTabsAndPreservesSpacesInWords)
{
    std::string word;
    std::string code;
    std::string message;
    int weight = -1;

    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine("包含 空格的词\tbao'han'kong'ge'de'ci\t123", word,
                                                                 code, weight, message));
    REQUIRE_EQ(word, std::string("包含 空格的词"));
    REQUIRE_EQ(code, std::string("bao'han'kong'ge'de'ci"));
    REQUIRE_EQ(weight, 123);

    REQUIRE(!SettingsDictionary::Validation::ParseCodedImportLine("普通词 putongci 10", word, code, weight, message));
    REQUIRE(!SettingsDictionary::Validation::ParseCodedImportLine("普通词\tputongci\t10\textra", word, code, weight,
                                                                  message));
}

TEST_CASE(CodedDictionaryImportAcceptsOptionalWeightAndRimeUserdb)
{
    std::string word;
    std::string code;
    std::string message;
    int weight = -1;

    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine("普通词\tputongci", word, code, weight, message));
    REQUIRE_EQ(word, std::string("普通词"));
    REQUIRE_EQ(code, std::string("putongci"));
    REQUIRE_EQ(weight, SettingsDictionary::Validation::kDefaultPinyinImportWeight);

    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine("你好\tni hao\tc=3 d=0.12 t=12345", word, code, weight,
                                                                 message));
    REQUIRE_EQ(word, std::string("你好"));
    REQUIRE_EQ(code, std::string("ni hao"));
    REQUIRE_EQ(weight, SettingsDictionary::Validation::kDefaultPinyinImportWeight);

    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine("西安\txi an\tc=1", word, code, weight, message));
    REQUIRE_EQ(word, std::string("西安"));
    REQUIRE_EQ(code, std::string("xi an"));
    REQUIRE_EQ(weight, SettingsDictionary::Validation::kDefaultPinyinImportWeight);

    REQUIRE(
        !SettingsDictionary::Validation::ParseCodedImportLine("普通词\tputongci\tabc", word, code, weight, message));
}

TEST_CASE(ImportLineSkipWalksYamlFrontMatterAndComments)
{
    bool in_yaml_header = false;
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("", in_yaml_header));
    REQUIRE(!in_yaml_header);
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("   ", in_yaml_header));
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("# Rime user dictionary", in_yaml_header));
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("#@/db_name\tluna_pinyin", in_yaml_header));
    REQUIRE(!SettingsDictionary::Validation::ShouldSkipImportLine("你好\tni hao\t1", in_yaml_header));

    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("---", in_yaml_header));
    REQUIRE(in_yaml_header);
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("name: luna_pinyin", in_yaml_header));
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("sort: by_weight", in_yaml_header));
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("  ---", in_yaml_header));
    REQUIRE(in_yaml_header);
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("...", in_yaml_header));
    REQUIRE(!in_yaml_header);
    REQUIRE(!SettingsDictionary::Validation::ShouldSkipImportLine("你好\tni hao", in_yaml_header));
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("  # comment after body", in_yaml_header));
}

TEST_CASE(CodedQuanpinImportAcceptsExportedApostropheTsv)
{
    std::string word;
    std::string code;
    std::string message;
    int weight = -1;
    quanpin::Segments segments;
    std::string normalized;

    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine("啊啊啊啊啊啊啊啊\ta'a'a'a'a'a'a'a\t41", word, code,
                                                                 weight, message));
    REQUIRE_EQ(word, std::string("啊啊啊啊啊啊啊啊"));
    REQUIRE_EQ(code, std::string("a'a'a'a'a'a'a'a"));
    REQUIRE_EQ(weight, 41);
    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin(code, segments, normalized, 8));
    REQUIRE_EQ(segments.size(), static_cast<size_t>(8));

    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine("阿巴拉提亚云海\ta'ba'la'ti'ya'yun'hai\t13", word,
                                                                 code, weight, message));
    REQUIRE_EQ(word, std::string("阿巴拉提亚云海"));
    REQUIRE_EQ(code, std::string("a'ba'la'ti'ya'yun'hai"));
    REQUIRE_EQ(weight, 13);
    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin(code, segments, normalized, 7));
    REQUIRE_EQ(normalized, std::string("a'ba'la'ti'ya'yun'hai"));
    REQUIRE_EQ(segments.size(), static_cast<size_t>(7));
}

namespace
{
// 单字码取自出厂 wubi86 词库（wubi86 WHERE length(value)=1，取 4 级全码，无 4 级时取最长码）。
// 环境/计算机/诸葛亮/中华人民共和国 在词库里有词条，下面写的期望值就是它们真实的 key；
// 张三/五笔输入法 词库里没有，期望值由规则推出。一并用它们当回归锚点。
SettingsDictionary::Validation::WubiCharCodes RealWubiCharCodes()
{
    using WubiCharCodes = SettingsDictionary::Validation::WubiCharCodes;
    return WubiCharCodes{
        {"环", "ggiy"}, {"境", "fujq"}, // 环境 -> ggfu
        {"五", "gghg"}, {"笔", "ttfn"}, {"输", "lwgj"}, {"入", "tyi"},
        {"法", "ifcy"}, {"计", "yfh"},  {"算", "thaj"}, {"机", "smn"}, // 计算机 -> ytsm
        {"中", "khk"},  {"华", "wxfj"}, {"人", "wwww"}, {"民", "nav"},
        {"共", "awu"},  {"和", "tkg"},  {"国", "lgyi"}, // 中华人民共和国 -> kwwl
        {"张", "xtay"}, {"三", "dggg"},                 // 张三 -> xtdg
        {"诸", "yftj"}, {"葛", "ajqn"}, {"亮", "ypmb"}, // 诸葛亮 -> yayp
        {"节", "abj"},  {"式", "aa"},
    };
}

std::string Compose(const std::string &word)
{
    return SettingsDictionary::Validation::ComposeWubiPhraseCode(word, RealWubiCharCodes());
}

// 指向一个空目录，让每条分支都在写库之前就返回——本文件不碰任何真实词库。
// ime_paths 每次调用都重读这个环境变量（不缓存），所以改动不会漏给别的用例。
class ScopedDataDir
{
  public:
    explicit ScopedDataDir(const std::filesystem::path &path)
    {
        const std::wstring value = path.wstring();
        wchar_t buffer[32768];
        const DWORD length = GetEnvironmentVariableW(kName, buffer, 32768);
        had_previous_ = length != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
        previous_.assign(buffer, length);
        SetEnvironmentVariableW(kName, value.c_str());
    }
    ~ScopedDataDir()
    {
        SetEnvironmentVariableW(kName, had_previous_ ? previous_.c_str() : nullptr);
    }

    ScopedDataDir(const ScopedDataDir &) = delete;
    ScopedDataDir &operator=(const ScopedDataDir &) = delete;

  private:
    static constexpr const wchar_t *kName = L"METASEQUOIA_IME_DATA_DIR";
    std::wstring previous_;
    bool had_previous_ = false;
};
} // namespace

TEST_CASE(WubiPhraseCodeTakesTwoLettersFromEachOfTwoCharWords)
{
    // 2 字词：首字前 2 位 + 次字前 2 位
    REQUIRE_EQ(Compose("环境"), std::string("ggfu"));
    REQUIRE_EQ(Compose("张三"), std::string("xtdg"));
}

TEST_CASE(WubiPhraseCodeTakesOneOneThenTwoForThreeCharWords)
{
    // 3 字词：首字 1 位 + 次字 1 位 + 末字前 2 位
    REQUIRE_EQ(Compose("计算机"), std::string("ytsm"));
    REQUIRE_EQ(Compose("诸葛亮"), std::string("yayp"));
}

TEST_CASE(WubiPhraseCodeTakesTheLastCharNotTheFourthForLongWords)
{
    // 4 字以上：首、次、三、末各 1 位。末字是「最后一个字」而非第 4 个字——
    // 中华人民共和国里 民/共/和 都不参与取码，用的是末尾的 国。
    REQUIRE_EQ(Compose("五笔输入法"), std::string("gtli"));
    REQUIRE_EQ(Compose("中华人民共和国"), std::string("kwwl"));
    // 实现若误取第 4 个字（民）会得到 kwwn，与 kwwl 不同，这条断言能区分。
}

TEST_CASE(WubiPhraseCodeKeepsSingleCharAtItsNaturalLength)
{
    // 单字保持自然码长且不补 z：wubi86 里「节」存的就是 abj，补成 abjz 会造出重复词条。
    REQUIRE_EQ(Compose("节"), std::string("abj"));
    REQUIRE_EQ(Compose("式"), std::string("aa"));
    REQUIRE_EQ(Compose("国"), std::string("lgyi"));
}

TEST_CASE(WubiPhraseCodeFailsWhenAnyCharacterHasNoCode)
{
    // 「观」在出厂 wubi86 里其实有码（cmqn），这里用它是因为本用例构造的码表里没放它——
    // 纯函数只认传进来的表，不查库。
    REQUIRE(!Compose("环").empty());
    REQUIRE(Compose("环观").empty());
    REQUIRE(Compose("观").empty());
    REQUIRE(Compose("").empty());
    REQUIRE(Compose("节观").empty()); // 首字有码也不放过
}

TEST_CASE(WubiImportDefaultWeightSitsOnTheWubiScaleNotThePinyinScale)
{
    // 出厂词库实测：拼音 2 字词 p50=2805/max=9931703，五笔 2 字词 p50=10/max=110。
    // 同一个 10000 落在拼音 p50~p60 合理区间，搬到五笔就是全库 max 的 91 倍。
    REQUIRE_EQ(SettingsDictionary::Validation::kDefaultPinyinImportWeight, 10000);
    REQUIRE_EQ(SettingsDictionary::Validation::kDefaultWubiImportWeight, 30);

    std::string word;
    std::string code;
    std::string message;
    int weight = -1;
    // 不传 default_weight 时保持拼音缺省，拼音导入行为不变。
    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine("环境\tggfu", word, code, weight, message));
    REQUIRE_EQ(weight, 10000);
    // 五笔导入显式传自己的刻度。
    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine(
        "环境\tggfu", word, code, weight, message, SettingsDictionary::Validation::kDefaultWubiImportWeight));
    REQUIRE_EQ(weight, 30);
    // 文件里显式给了权重时不改写。
    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine(
        "环境\tggfu\t55", word, code, weight, message, SettingsDictionary::Validation::kDefaultWubiImportWeight));
    REQUIRE_EQ(weight, 55);
}

TEST_CASE(HansImportIsRoutedByTheTargetDictionary)
{
    const std::filesystem::path empty_data_dir = std::filesystem::temp_directory_path() / L"msime-empty-hans-import";
    std::filesystem::create_directories(empty_data_dir);
    const ScopedDataDir scoped(empty_data_dir);

    const auto response_for = [](const char *dictionary) {
        return SettingsDictionary::HandleRequest(
            {{"dictionary", dictionary}, {"action", "importHans"}, {"content", "环境"}});
    };
    const auto message_of = [](const boost::json::object &response) {
        return std::string(response.at("message").as_string());
    };

    // 英文与快捷短语词库收不了纯汉字。改动前这两种请求会一路落到拼音分支，
    // 把汉字写进拼音表；必须明确报错，不能静默成功。
    for (const char *dictionary : {"english", "quick"})
    {
        const auto response = response_for(dictionary);
        REQUIRE(!response.at("ok").as_bool());
        REQUIRE_EQ(message_of(response), std::string("该词库不支持纯汉字导入"));
    }

    // 五笔走 wubi86 单字码组合、拼音走注音引擎，两条分支的报错文案不同，
    // 正好能证明请求没有落到对方那条路径上。空数据目录下两条都开不了库。
    const auto wubi = response_for("wubi");
    REQUIRE(!wubi.at("ok").as_bool());
    REQUIRE_EQ(message_of(wubi).rfind("打开五笔词库失败", 0), static_cast<size_t>(0));

    const auto quanpin = response_for("quanpin");
    REQUIRE(!quanpin.at("ok").as_bool());
    REQUIRE(message_of(quanpin).rfind("打开五笔词库失败", 0) != 0);
    REQUIRE(message_of(quanpin) != "该词库不支持纯汉字导入");
}
