#include "collocation_model.h"

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
namespace
{

constexpr const wchar_t *kModelHost = L"github.com";
constexpr const wchar_t *kModelPath = L"/amzxyz/RIME-LMDG/releases/download/LTS/wanxiang-lts-zh-hans.gram";
constexpr const char *kModelId = collocation::kDefaultModelId;

std::filesystem::path ModelDirectory()
{
    return metasequoia::data_directory() / "models" / kModelId;
}

std::filesystem::path ModelFile()
{
    return ModelDirectory() / (std::string(kModelId) + ".gram");
}

std::filesystem::path PartFile()
{
    // 同一卷上的临时名，下载完成后原子 rename 到位。
    return metasequoia::data_directory() / "models" / (std::string(kModelId) + ".gram.part");
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

std::mutex g_mutex;
DownloadState g_state = DownloadState::Idle;
int g_progress = 0;
std::string g_error;

void SetState(DownloadState state, int progress, std::string error)
{
    std::lock_guard lock(g_mutex);
    g_state = state;
    g_progress = progress;
    g_error = std::move(error);
}

// 后台线程：下载 -> 校验 -> 原子落位。全程只碰 PartFile，失败时不留半个模型
// 在正式位置上。GitHub 的 release 资产经 302 跳转到 CDN，显式放开自动重定向。
void DownloadThread()
{
    SetState(DownloadState::Downloading, 0, {});

    std::error_code fs_error;
    const std::filesystem::path directory = ModelDirectory();
    std::filesystem::create_directories(directory, fs_error);
    if (fs_error)
    {
        SetState(DownloadState::Error, 0, "无法创建模型目录");
        return;
    }

    HINTERNET session = WinHttpOpen(L"MetasequoiaImeServer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
    {
        SetState(DownloadState::Error, 0, "无法初始化网络");
        return;
    }
    // 400MB 不是快操作：连接阶段给足超时，收发阶段不设上限靠进度与失败兜底。
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 0);
    HINTERNET connection = WinHttpConnect(session, kModelHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = nullptr;
    if (connection != nullptr)
    {
        request = WinHttpOpenRequest(connection, L"GET", kModelPath, nullptr, WINHTTP_NO_REFERER,
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
        out.open(PartFile(), std::ios::binary | std::ios::trunc);
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
            SetState(DownloadState::Downloading, progress, {});
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
        SetState(DownloadState::Error, 0, status != 0 && status != HTTP_STATUS_OK ? "下载响应异常" : "下载中断");
        std::error_code cleanup;
        std::filesystem::remove(PartFile(), cleanup);
        return;
    }

    // 传输完整性：收完的字节数必须等于响应声明的 Content-Length。声明缺失时跳过，
    // 交给下面的格式校验兜底。
    if (expected > 0 && received != expected)
    {
        SetState(DownloadState::Error, 0, "下载字节数与响应声明不符");
        std::error_code cleanup;
        std::filesystem::remove(PartFile(), cleanup);
        return;
    }

    // 格式锁：唯一真正拦下坏模型的检查，也是「已下载」的定义——能被引擎读取端
    // 打开（魔数与双数组边界合法）就放行落位，打开不了的一律按损坏拒绝。
    std::string format_error;
    if (!ValidateFormat(PartFile(), format_error))
    {
        SetState(DownloadState::Error, 0, "模型格式校验失败（上游可能改了格式）：" + format_error);
        std::error_code cleanup;
        std::filesystem::remove(PartFile(), cleanup);
        return;
    }

    // ValidateFormat 的局部 GramDb 已析构、映射已解除，否则下面的 rename 会被
    // 以 FILE_SHARE_READ 打开的旧句柄拒绝。
    std::error_code rename_error;
    std::filesystem::rename(PartFile(), ModelFile(), rename_error);
    if (rename_error)
    {
        SetState(DownloadState::Error, 0, "模型落位失败");
        return;
    }

    // CC-BY-4.0 的署名义务随模型走：NOTICE 与模型同目录，注明来源与许可。
    std::error_code size_error;
    const long long model_bytes = static_cast<long long>(std::filesystem::file_size(ModelFile(), size_error));
    std::ofstream notice(ModelDirectory() / "NOTICE.md", std::ios::binary | std::ios::trunc);
    if (notice)
    {
        notice << "# 万象语法模型（" << kModelId << "）\n\n"
               << "- 来源：https://github.com/amzxyz/RIME-LMDG/releases/tag/LTS\n"
               << "- 许可：CC-BY-4.0（© amzxyz / RIME-LMDG 项目）\n"
               << "- 文件：" << kModelId << ".gram（" << model_bytes << " 字节）\n";
    }
    SetState(DownloadState::Idle, 100, {});
}

} // namespace

ModelStatus GetModelStatus()
{
    std::error_code fs_error;
    // 落位发生在格式校验之后，所以「文件存在」即代表曾经通过校验。这里不再比对
    // 字节数：钉死它就等于钉死上游的每次重训。
    if (std::filesystem::exists(ModelFile(), fs_error) && !fs_error)
    {
        return {"ready", 100, {}};
    }
    std::lock_guard lock(g_mutex);
    switch (g_state)
    {
    case DownloadState::Downloading:
        return {"downloading", g_progress, {}};
    case DownloadState::Error:
        return {"error", 0, g_error};
    default:
        return {"absent", 0, {}};
    }
}

bool StartDownload()
{
    {
        std::lock_guard lock(g_mutex);
        if (g_state == DownloadState::Downloading)
            return true;
        g_state = DownloadState::Downloading;
        g_progress = 0;
        g_error.clear();
    }
    std::thread(DownloadThread).detach();
    return true;
}

} // namespace collocation
