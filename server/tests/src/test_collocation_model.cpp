// octagram 模型目录（catalog）与激活链路的测试：快照集合形状、modelId 下载/删除守卫、
// 激活写入与「空激活回退内置推荐包」的解析语义。消息 schema 的形状（collocationModel
// Download/Delete 带 modelId）由 webview_contract 的 fixtures 覆盖，这里钉 Server 侧行为。
#include "tests/includes/test_framework.h"
#include "src/config/ime_config.h"
#include "src/session/engine_input_session.h"
#include "src/settings/collocation_model.h"

#include <Windows.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace
{
// 环境变量隔离：进入时保存原值，退出时恢复（没有原值就删除）。
// 读写必须走 CRT 一侧：data_directory() 经 _wdupenv_s 读 CRT 环境副本，Win32 的
// SetEnvironmentVariableW 对它不可见（UCRT 只在 _wputenv_s 时同步缓存）——这是
// test_custom_shuangpin 同款写法的原因。
class ScopedEnv
{
  public:
    ScopedEnv(const wchar_t *name, const std::wstring &value) : name_(name)
    {
        wchar_t *previous = nullptr;
        size_t size = 0;
        if (_wdupenv_s(&previous, &size, name) == 0 && previous != nullptr)
        {
            had_previous_ = true;
            previous_ = previous;
            free(previous);
        }
        _wputenv_s(name, value.c_str());
    }
    ~ScopedEnv()
    {
        restore();
    }

    // 显式还原（幂等）：拆卸时环境变量必须先于 InitImeConfig() 还原，g_config_path 才能
    // 重算回真实位置；成员析构阶段会再调一次，靠标记跳过。
    void restore()
    {
        if (restored_)
        {
            return;
        }
        restored_ = true;
        // CRT 语义：空值即删除该变量。
        _wputenv_s(name_.c_str(), had_previous_ ? previous_.c_str() : L"");
    }

    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;

  private:
    std::wstring name_;
    std::wstring previous_;
    bool had_previous_ = false;
    bool restored_ = false;
};

// 一次性数据根 + 配置根。模型目录经 data_directory() 读环境变量（每次调用都读，切换
// 立即生效）；配置 setter 走 g_config_path，需要 InitImeConfig() 在环境变量就位后重算
// ——这是 test_config_non_ascii_path 验证过的顺序。
class ScopedCollocationEnvironment
{
  public:
    ScopedCollocationEnvironment()
        : root_(std::filesystem::temp_directory_path() /
                (L"msime-collocation-model-test-" + std::to_wstring(GetCurrentProcessId()))),
          local_app_data_(L"LOCALAPPDATA", root_.wstring()),
          config_dir_(L"METASEQUOIA_IME_CONFIG_DIR", (root_ / L"metasequoiaime").wstring()),
          data_dir_(L"METASEQUOIA_IME_DATA_DIR", (root_ / L"metasequoiaime").wstring())
    {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
        std::filesystem::create_directories(root_ / L"metasequoiaime", ec);
        std::filesystem::copy_file(MSIME_DEFAULT_CONFIG_PATH, root_ / L"metasequoiaime" / L"config.default.toml",
                                   std::filesystem::copy_options::overwrite_existing, ec);
        REQUIRE(!ec);
        InitImeConfig();
    }
    ~ScopedCollocationEnvironment()
    {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
        // 先还原环境变量再重算 g_config_path：配置路径是进程级缓存，悬在已删除的测试根上
        // 会让本测试之后的所有配置写入失败（顺序约定同 test_config_non_ascii_path）。
        data_dir_.restore();
        config_dir_.restore();
        local_app_data_.restore();
        InitImeConfig();
    }
    ScopedCollocationEnvironment(const ScopedCollocationEnvironment &) = delete;
    ScopedCollocationEnvironment &operator=(const ScopedCollocationEnvironment &) = delete;

    std::filesystem::path models_dir() const
    {
        return root_ / L"metasequoiaime" / L"models";
    }

  private:
    std::filesystem::path root_;
    ScopedEnv local_app_data_;
    ScopedEnv config_dir_;
    ScopedEnv data_dir_;
};

// 在数据根里放一个「已落位」的模型包。落位只在格式校验之后发生，所以查询侧把文件存在
// 直接当 ready；测试里写几个字节即可驱动同一条判定，不需要真实 .gram。
void SeedModelFile(const std::filesystem::path &models_dir, const std::wstring &id)
{
    std::error_code ec;
    std::filesystem::create_directories(models_dir / id, ec);
    REQUIRE(!ec);
    std::ofstream output(models_dir / id / (id + L".gram"), std::ios::binary | std::ios::trunc);
    REQUIRE(static_cast<bool>(output));
    output << "gram";
}
} // namespace

TEST_CASE(collocation_catalog_lists_four_builtin_models)
{
    const auto &catalog = collocation::Catalog();
    REQUIRE_EQ(catalog.size(), static_cast<std::size_t>(4));
    // 次序即设置页展示次序；首条目必须是内置推荐包（激活留空的回退目标）。
    REQUIRE_EQ(std::string(catalog[0].id), std::string(collocation::kDefaultModelId));
    REQUIRE_EQ(std::string(catalog[1].id), std::string("zh-hans-t-essay-bgw"));
    REQUIRE_EQ(std::string(catalog[2].id), std::string("zh-hans-t-essay-bgw-compact"));
    REQUIRE_EQ(std::string(catalog[3].id), std::string("zh-moqi"));
    // 每个条目都要带齐下载直链与展示字段；zh-moqi 的许可状态必须如实标注
    // （构建链未声明许可），NOTICE 与设置页都从这里取词。
    for (const auto &entry : catalog)
    {
        REQUIRE(entry.host != nullptr && *entry.host != L'\0');
        REQUIRE(entry.path != nullptr && entry.path[0] == L'/');
        REQUIRE(entry.size_hint != nullptr && *entry.size_hint != '\0');
        REQUIRE(entry.license != nullptr && *entry.license != '\0');
    }
    REQUIRE(std::string(catalog[3].license_note).find("构建链未声明许可") != std::string::npos);
}

TEST_CASE(collocation_statuses_cover_catalog_and_follow_disk)
{
    ScopedCollocationEnvironment env;
    auto statuses = collocation::GetModelStatuses();
    // 快照集合形状：逐目录条目给出一项，不多不少，未下载即 absent。
    REQUIRE_EQ(statuses.size(), collocation::Catalog().size());
    for (const auto &entry : collocation::Catalog())
    {
        REQUIRE(statuses.count(entry.id) == 1);
        REQUIRE_EQ(statuses[entry.id].state, std::string("absent"));
    }
    // 磁盘优先：文件存在即 ready（落位发生在格式校验之后），progress 顶格。
    SeedModelFile(env.models_dir(), L"zh-moqi");
    statuses = collocation::GetModelStatuses();
    REQUIRE_EQ(statuses["zh-moqi"].state, std::string("ready"));
    REQUIRE_EQ(statuses["zh-moqi"].progress, 100);
}

TEST_CASE(collocation_download_and_delete_reject_ids_outside_catalog)
{
    ScopedCollocationEnvironment env;
    // modelId 消息的 schema 形状由 webview_contract 的 fixtures 把守；这里钉 Server 侧
    // 对未知 id 的拒绝，下载与删除走同一守卫入口，路径拼接不允许被带出 models/。
    REQUIRE(!collocation::StartDownload("not-in-catalog"));
    REQUIRE(!collocation::DeleteModel("not-in-catalog"));
    REQUIRE(!collocation::DeleteModel("../../escape"));
    // 已就绪的包不重下：幂等返回 true 且不启动下载线程（磁盘检查先于任何 I/O）。
    SeedModelFile(env.models_dir(), L"zh-moqi");
    REQUIRE(collocation::StartDownload("zh-moqi"));
}

TEST_CASE(collocation_activation_write_and_empty_fallback_delete_guard)
{
    ScopedCollocationEnvironment env;
    // 激活写入：setter 落盘并同步全局；空串 = 回退内置推荐包，也是「切回内置」的写法。
    REQUIRE(SetConfiguredAssocSentenceCollocationModel("zh-moqi"));
    REQUIRE_EQ(GetConfiguredAssocSentenceCollocationModel(), std::string("zh-moqi"));
    REQUIRE(SetConfiguredAssocSentenceCollocationModel(""));
    REQUIRE(GetConfiguredAssocSentenceCollocationModel().empty());

    // 空激活的删除守卫：生效解析 = 内置推荐包，删除被拒绝且文件原封不动。
    SeedModelFile(env.models_dir(), L"wanxiang-lts-zh-hans");
    SeedModelFile(env.models_dir(), L"zh-moqi");
    REQUIRE(!collocation::DeleteModel(collocation::kDefaultModelId));
    REQUIRE(std::filesystem::exists(env.models_dir() / L"wanxiang-lts-zh-hans" / L"wanxiang-lts-zh-hans.gram"));
    // 非激活的包可以删，删完整个目录消失。
    REQUIRE(collocation::DeleteModel("zh-moqi"));
    REQUIRE(!std::filesystem::exists(env.models_dir() / L"zh-moqi"));

    // 显式激活 zh-moqi 后守卫换目标：它不可删，内置包不再受保护。
    SeedModelFile(env.models_dir(), L"zh-moqi");
    REQUIRE(SetConfiguredAssocSentenceCollocationModel("zh-moqi"));
    REQUIRE(!collocation::DeleteModel("zh-moqi"));
    REQUIRE(std::filesystem::exists(env.models_dir() / L"zh-moqi" / L"zh-moqi.gram"));
    REQUIRE(collocation::DeleteModel(collocation::kDefaultModelId));
    REQUIRE(!std::filesystem::exists(env.models_dir() / L"wanxiang-lts-zh-hans"));
    // 还原激活状态，不把测试残留带给同进程的后续用例。
    REQUIRE(SetConfiguredAssocSentenceCollocationModel(""));
}

TEST_CASE(collocation_path_resolution_falls_back_to_default_on_empty_id)
{
    ScopedCollocationEnvironment env;
    // 什么都没下载：解析返回空串，调用方按整句加成全关处理。
    REQUIRE(ResolveCollocationModelPath("").empty());
    REQUIRE(ResolveCollocationModelPath("zh-moqi").empty());
    // 空激活回退内置推荐包：解析落在内置 id 的确定性布局上，而不是拿空 id 拼路径。
    SeedModelFile(env.models_dir(), L"wanxiang-lts-zh-hans");
    const std::string resolved = ResolveCollocationModelPath("");
    REQUIRE(!resolved.empty());
    REQUIRE(resolved.find("wanxiang-lts-zh-hans") != std::string::npos);
    // 显式 id 只解析自己的文件，缺席仍返回空。
    REQUIRE(ResolveCollocationModelPath("zh-moqi").empty());
}
