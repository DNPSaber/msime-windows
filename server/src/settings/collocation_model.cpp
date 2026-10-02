#include "collocation_model.h"

#include "config/ime_config.h"
#include "engine/core/data_path.h"
#include "engine/ngram/octagram/octagram_gram.h"

#include <Windows.h>
#include <winhttp.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace collocation
{

// 编译期内置目录，首条目是推荐包。收录新模型 = 改这里发版，条目随签名安装包走；
// 不引入动态目录源，避免免签分发渠道。zh-moqi 的构建链未声明许可，license_note 如实
// 说明并标实验。
const std::vector<CatalogEntry> &Catalog()
{
    static const std::vector<CatalogEntry> kCatalog = {
        {kRecommendedModelId, "万象 LTS（推荐）", L"github.com",
         L"/amzxyz/RIME-LMDG/releases/download/LTS/wanxiang-lts-zh-hans.gram", "约 390 MB", "CC-BY-4.0",
         "© amzxyz / RIME-LMDG 项目"},
        {"zh-hans-t-essay-bgw", "八股文·词级", L"github.com",
         L"/lotem/rime-octagram-data/releases/download/20260712/zh-hans-t-essay-bgw.gram", "约 197 MB", "LGPL", ""},
        {"zh-hans-t-essay-bgw-compact", "八股文·词级紧凑", L"github.com",
         L"/lotem/rime-octagram-data/releases/download/20260712/zh-hans-t-essay-bgw-compact.gram", "约 39 MB", "LGPL",
         ""},
        {"zh-moqi", "白霜（实验）", L"raw.githubusercontent.com", L"/gaboolic/rime-frost/master/zh-moqi.gram",
         "约 7 MB", "GPL-3.0", "随 GPL-3.0 仓库（gaboolic/rime-frost）分发，构建链未声明许可，实验性收录"},
    };
    return kCatalog;
}

namespace
{

const CatalogEntry *FindEntry(const std::string &model_id)
{
    for (const auto &entry : Catalog())
    {
        if (model_id == entry.id)
            return &entry;
    }
    return nullptr;
}

std::filesystem::path ModelDirectory(const std::string &model_id)
{
    return metasequoia::data_directory() / "models" / model_id;
}

std::filesystem::path ModelFile(const std::string &model_id)
{
    return ModelDirectory(model_id) / (model_id + ".gram");
}

std::filesystem::path PartFile(const std::string &model_id)
{
    // 同一卷上的临时名，下载完成后原子 rename 到位；放进模型自己的目录，多个模型的
    // 下载互不碰撞。
    return ModelDirectory(model_id) / (model_id + ".gram.part");
}

// 目录条目的 host/path 都是 ASCII（域名与仓库路径），NOTICE.md 是 UTF-8 文本，写之前把
// 宽字符转回来；走 CP_UTF8 而不是逐字节截断，将来条目里出现非 ASCII 也不会产出坏字节。
std::string Utf8FromWide(const wchar_t *wide)
{
    if (wide == nullptr)
        return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(bytes > 0 ? static_cast<size_t>(bytes) - 1 : 0, '\0');
    if (bytes > 1)
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow.data(), bytes, nullptr, nullptr);
    return narrow;
}

// 格式锁：唯一真正拦下坏模型的检查。让引擎读取端自己开一次，Magic 与双数组边界
// 都在 open() 里，越界在 set_array 之前就被拒，且不按文件自带的长度做堆分配。
// 刻意用局部实例而不是 shared_gram_db——后者按路径永久缓存，会一直攥着 .part 的
// 映射，而映射是以 FILE_SHARE_READ 打开的（没有 FILE_SHARE_DELETE），改名将失败。
bool ValidateFormat(const std::filesystem::path &file, std::string &error)
{
    gram::GramDb db;
    if (db.open(file))
    {
        return true;
    }
    error = db.error();
    return false;
}

enum class DownloadState
{
    Idle,
    Downloading,
    Error,
};

struct RuntimeState
{
    DownloadState state = DownloadState::Idle;
    int progress = 0;
    std::string error;
};

std::mutex g_mutex;
// 按 id 的下载态，map 大小就是目录条目数。成功后该 id 留下一个 Idle 条目，无碍：
// 查询侧以磁盘为准，runtime 态只在文件缺席时补充 downloading/error。
std::map<std::string, RuntimeState> g_states;

void SetState(const std::string &model_id, DownloadState state, int progress, std::string error)
{
    std::lock_guard lock(g_mutex);
    RuntimeState &runtime = g_states[model_id];
    runtime.state = state;
    runtime.progress = progress;
    runtime.error = std::move(error);
}

// 后台线程：下载 -> 校验 -> 原子落位。全程只碰 PartFile，失败时不留半个模型
// 在正式位置上。条目按值收进线程：detached 线程的生命周期长于调用栈，host/path/id
// 必须自持。release 资产与 raw 文件都经 302 跳转到 CDN，显式放开自动重定向。
void DownloadThread(CatalogEntry entry)
{
    const std::string model_id = entry.id;
    SetState(model_id, DownloadState::Downloading, 0, {});

    std::error_code fs_error;
    const std::filesystem::path directory = ModelDirectory(model_id);
    std::filesystem::create_directories(directory, fs_error);
    if (fs_error)
    {
        SetState(model_id, DownloadState::Error, 0, "无法创建模型目录");
        return;
    }

    HINTERNET session = WinHttpOpen(L"MetasequoiaImeServer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
    {
        SetState(model_id, DownloadState::Error, 0, "无法初始化网络");
        return;
    }
    // 400MB 不是快操作：连接阶段给足超时，收发阶段不设上限靠进度与失败兜底。
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 0);
    HINTERNET connection = WinHttpConnect(session, entry.host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = nullptr;
    if (connection != nullptr)
    {
        request = WinHttpOpenRequest(connection, L"GET", entry.path, nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (request != nullptr)
        {
            DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
            WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        }
    }
    bool ok = connection != nullptr && request != nullptr &&
              WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr);
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (ok)
    {
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
        ok = status == HTTP_STATUS_OK;
    }

    // 总长取响应自己声明的 Content-Length，不再钉死字节数常量：上游重训会让字节数
    // 变，而这个值顺带充当截断检测的基准（收完比对一次），比钉常量更贴近实际。
    long long expected = 0;
    if (ok)
    {
        DWORD length = 0;
        DWORD length_size = sizeof(length);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &length, &length_size, WINHTTP_NO_HEADER_INDEX))
        {
            expected = length;
        }
    }

    std::ofstream out;
    if (ok)
    {
        out.open(PartFile(model_id), std::ios::binary | std::ios::trunc);
        ok = static_cast<bool>(out);
    }

    long long received = 0;
    while (ok)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
        {
            ok = false;
            break;
        }
        if (available == 0)
            break;
        std::vector<char> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &read) || read == 0)
        {
            ok = false;
            break;
        }
        out.write(buffer.data(), static_cast<std::streamsize>(read));
        if (!out)
        {
            ok = false;
            break;
        }
        received += read;
        if (expected > 0)
        {
            const int progress = static_cast<int>((std::min)(100LL, received * 100LL / expected));
            SetState(model_id, DownloadState::Downloading, progress, {});
        }
    }
    if (request != nullptr)
        WinHttpCloseHandle(request);
    if (connection != nullptr)
        WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    out.close();

    if (!ok)
    {
        SetState(model_id, DownloadState::Error, 0,
                 status != 0 && status != HTTP_STATUS_OK ? "下载响应异常" : "下载中断");
        std::error_code cleanup;
        std::filesystem::remove(PartFile(model_id), cleanup);
        return;
    }

    // 传输完整性：收完的字节数必须等于响应声明的 Content-Length。声明缺失时跳过，
    // 交给下面的格式校验兜底。
    if (expected > 0 && received != expected)
    {
        SetState(model_id, DownloadState::Error, 0, "下载字节数与响应声明不符");
        std::error_code cleanup;
        std::filesystem::remove(PartFile(model_id), cleanup);
        return;
    }

    // 格式锁：唯一真正拦下坏模型的检查，也是「已下载」的定义——能被引擎读取端
    // 打开（魔数与双数组边界合法）就放行落位，打开不了的一律按损坏拒绝。
    std::string format_error;
    if (!ValidateFormat(PartFile(model_id), format_error))
    {
        SetState(model_id, DownloadState::Error, 0, "模型格式校验失败（上游可能改了格式）：" + format_error);
        std::error_code cleanup;
        std::filesystem::remove(PartFile(model_id), cleanup);
        return;
    }

    // ValidateFormat 的局部 GramDb 已析构、映射已解除，否则下面的 rename 会被
    // 以 FILE_SHARE_READ 打开的旧句柄拒绝。
    std::error_code rename_error;
    std::filesystem::rename(PartFile(model_id), ModelFile(model_id), rename_error);
    if (rename_error)
    {
        SetState(model_id, DownloadState::Error, 0, "模型落位失败");
        return;
    }

    // 署名义务随模型走：NOTICE 与模型同目录，注明来源与许可；各条目许可不同，
    // zh-moqi 的许可状态在 license_note 里如实说明。
    std::error_code size_error;
    const long long model_bytes = static_cast<long long>(std::filesystem::file_size(ModelFile(model_id), size_error));
    std::ofstream notice(ModelDirectory(model_id) / "NOTICE.md", std::ios::binary | std::ios::trunc);
    if (notice)
    {
        const std::string note = entry.license_note == nullptr ? std::string() : std::string(entry.license_note);
        notice << "# " << entry.display_name << "（" << model_id << "）\n\n"
               << "- 来源：https://" << Utf8FromWide(entry.host) << Utf8FromWide(entry.path) << "\n"
               << "- 许可：" << entry.license << (note.empty() ? "" : "（" + note + "）") << "\n"
               << "- 文件：" << model_id << ".gram（" << model_bytes << " 字节）\n";
    }
    SetState(model_id, DownloadState::Idle, 100, {});
}

} // namespace

std::map<std::string, ModelStatus> GetModelStatuses()
{
    std::map<std::string, ModelStatus> statuses;
    for (const auto &entry : Catalog())
    {
        // 落位发生在格式校验之后，所以「文件存在」即代表曾经通过校验。这里不再比对
        // 字节数：钉死它就等于钉死上游的每次重训。
        std::error_code fs_error;
        if (std::filesystem::exists(ModelFile(entry.id), fs_error) && !fs_error)
        {
            statuses[entry.id] = {"ready", 100, {}};
            continue;
        }
        std::lock_guard lock(g_mutex);
        const auto it = g_states.find(entry.id);
        if (it == g_states.end() || it->second.state == DownloadState::Idle)
            statuses[entry.id] = {"absent", 0, {}};
        else if (it->second.state == DownloadState::Downloading)
            statuses[entry.id] = {"downloading", it->second.progress, {}};
        else
            statuses[entry.id] = {"error", 0, it->second.error};
    }
    return statuses;
}

bool StartDownload(const std::string &model_id)
{
    const CatalogEntry *entry = FindEntry(model_id);
    if (entry == nullptr)
        return false;
    // 已存在且通过格式校验的包不重下：落位只在格式校验之后发生，文件存在即曾通过校验，
    // 重下只会白白覆盖一份好包。
    std::error_code fs_error;
    if (std::filesystem::exists(ModelFile(model_id), fs_error) && !fs_error)
        return true;
    {
        std::lock_guard lock(g_mutex);
        // 单网络槽：同一时刻至多一个下载，不做并发队列。同 id 重复请求幂等；其他 id
        // 忙碌时拒绝，页面在下载态禁用其余下载按钮。
        for (const auto &[id, runtime] : g_states)
        {
            if (runtime.state == DownloadState::Downloading)
                return id == model_id;
        }
        RuntimeState &runtime = g_states[model_id];
        runtime.state = DownloadState::Downloading;
        runtime.progress = 0;
        runtime.error.clear();
    }
    std::thread(DownloadThread, *entry).detach();
    return true;
}

bool DeleteModel(const std::string &model_id)
{
    if (FindEntry(model_id) == nullptr)
        return false;
    {
        std::lock_guard lock(g_mutex);
        const auto it = g_states.find(model_id);
        if (it != g_states.end() && it->second.state == DownloadState::Downloading)
            return false;
    }
    // 当前显式激活的包不许删：删掉正在使用的包会让整句加成静默失效，输入侧看不到任何
    // 报错。激活值留空（未选择）时没有受保护的目标，任何包都可删。
    if (model_id == GetConfiguredAssocSentenceCollocationModel())
        return false;
    std::error_code error;
    // .gram 与 NOTICE.md 都在这个目录里，整目录删除即卸载；目录不存在时 remove_all
    // 无错返回，删除因此是幂等的。
    std::filesystem::remove_all(ModelDirectory(model_id), error);
    return !error;
}

} // namespace collocation
