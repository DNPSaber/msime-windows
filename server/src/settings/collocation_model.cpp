#include "collocation_model.h"

#include "engine/core/data_path.h"

#include <Windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace collocation
{
namespace
{

// 模型包的钉死输入。bytes 与 sha256 来自
// https://github.com/amzxyz/RIME-LMDG/releases/tag/LTS 的资产清单，
// 与 docs/research/2026-10-01-wanxiang-octagram-model.md 的评测基线同一份字节。
constexpr const wchar_t *kModelHost = L"github.com";
constexpr const wchar_t *kModelPath = L"/amzxyz/RIME-LMDG/releases/download/LTS/wanxiang-lts-zh-hans.gram";
constexpr const char *kModelSha256 = "ae43724a9aa02b4c493c603025549d2cba6fa6a67ae3ae040e55efdfeeaa1183";
constexpr long long kModelBytes = 409719852LL;
constexpr const char *kModelId = "wanxiang-lts-zh-hans";

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

std::string Sha256Hex(const std::filesystem::path &file)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    DWORD digest_length = 0, object_length = 0, callback = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digest_length),
                          sizeof(digest_length), &callback, 0) != 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_length),
                          sizeof(object_length), &callback, 0) != 0)
    {
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    std::vector<unsigned char> hash_object(object_length);
    if (BCryptCreateHash(algorithm, &hash, hash_object.data(), object_length, nullptr, 0, 0) == 0)
    {
        std::ifstream in(file, std::ios::binary);
        std::vector<char> buffer(1 << 20);
        bool ok = static_cast<bool>(in);
        while (ok && in)
        {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto read = in.gcount();
            if (read > 0)
                ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(read), 0) == 0;
        }
        if (ok)
        {
            std::vector<unsigned char> digest(digest_length);
            ok = BCryptFinishHash(hash, digest.data(), digest_length, 0) == 0;
            static const char *kHex = "0123456789abcdef";
            for (unsigned char byte : digest)
            {
                result.push_back(kHex[byte >> 4]);
                result.push_back(kHex[byte & 0x0F]);
            }
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
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
        const long long total = kModelBytes;
        if (total > 0)
        {
            const int progress = static_cast<int>((std::min)(100LL, received * 100LL / total));
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

    // 字节级校验：LTS tag 会被上游原地重传，这一步是防漂移的唯一闸门。
    if (std::filesystem::file_size(PartFile()) != static_cast<unsigned long long>(kModelBytes))
    {
        SetState(DownloadState::Error, 0, "下载字节数与锁定值不符");
        std::error_code cleanup;
        std::filesystem::remove(PartFile(), cleanup);
        return;
    }
    const std::string digest = Sha256Hex(PartFile());
    if (digest != kModelSha256)
    {
        SetState(DownloadState::Error, 0, "SHA256 与锁定值不符，模型可能已被上游更换");
        std::error_code cleanup;
        std::filesystem::remove(PartFile(), cleanup);
        return;
    }

    std::filesystem::rename(PartFile(), ModelFile(), fs_error);
    if (fs_error)
    {
        SetState(DownloadState::Error, 0, "模型落位失败");
        return;
    }

    // CC-BY-4.0 的署名义务随模型走：NOTICE 与模型同目录，注明来源与锁定摘要。
    std::ofstream notice(ModelDirectory() / "NOTICE.md", std::ios::binary | std::ios::trunc);
    if (notice)
    {
        notice << "# 万象语法模型（" << kModelId << "）\n\n"
               << "- 来源：" << "https://github.com/amzxyz/RIME-LMDG/releases/tag/LTS\n"
               << "- 许可：CC-BY-4.0（© amzxyz / RIME-LMDG 项目）\n"
               << "- 文件：" << kModelId << ".gram（" << kModelBytes << " 字节）\n"
               << "- SHA256：" << kModelSha256 << "\n";
    }
    SetState(DownloadState::Idle, 100, {});
}

} // namespace

ModelStatus GetModelStatus()
{
    std::error_code fs_error;
    if (std::filesystem::exists(ModelFile(), fs_error) && !fs_error &&
        static_cast<long long>(std::filesystem::file_size(ModelFile(), fs_error)) == kModelBytes && !fs_error)
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
