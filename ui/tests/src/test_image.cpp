#include "tests/includes/test_framework.h"

#include "msimeui/Controls.h"
#include "msimeui/DeviceResources.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <wincodec.h>

using Microsoft::WRL::ComPtr;

namespace
{
class ImageFixture
{
  public:
    ImageFixture()
    {
        REQUIRE(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"STATIC", L"Image pixel test", WS_POPUP, 0, 0, 320, 256,
                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        REQUIRE(hwnd != nullptr);

        wchar_t directory[MAX_PATH] = {};
        REQUIRE(GetTempPathW(MAX_PATH, directory) != 0);
        path = std::wstring(directory) + L"msimeui-image-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
               std::to_wstring(GetTickCount64()) + L".png";
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        REQUIRE(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(factory.GetAddressOf()))));
        REQUIRE(SUCCEEDED(factory->CreateStream(stream.GetAddressOf())));
        REQUIRE(SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)));
        REQUIRE(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())));
        REQUIRE(SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)));
        REQUIRE(SUCCEEDED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)));
        REQUIRE(SUCCEEDED(frame->Initialize(nullptr)));
        REQUIRE(SUCCEEDED(frame->SetSize(132, 106)));
        REQUIRE(SUCCEEDED(frame->SetResolution(96.0, 96.0)));
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        REQUIRE(SUCCEEDED(frame->SetPixelFormat(&format)));
        REQUIRE(IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA));
        std::vector<uint8_t> pixels(132 * 106 * 4, 0);
        for (UINT y = 2; y < 104; ++y)
        {
            for (UINT x = 2; x < 130; ++x)
            {
                const size_t offset = (y * 132 + x) * 4;
                pixels[offset] = (x + y) % 2 ? 255 : 0;
                pixels[offset + 1] = (x + y) % 2 ? 0 : 255;
                pixels[offset + 3] = 255;
            }
        }
        REQUIRE(SUCCEEDED(frame->WritePixels(106, 132 * 4, static_cast<UINT>(pixels.size()), pixels.data())));
        REQUIRE(SUCCEEDED(frame->Commit()));
        REQUIRE(SUCCEEDED(encoder->Commit()));
    }

    ~ImageFixture()
    {
        DeleteFileW(path.c_str());
        DestroyWindow(hwnd);
        CoUninitialize();
    }

    HWND hwnd = nullptr;
    std::wstring path;
};

class Canvas
{
  public:
    Canvas(ID2D1DeviceContext *dc, float dpi) : dc_(dc)
    {
        const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        REQUIRE(SUCCEEDED(dc->CreateBitmap(D2D1::SizeU(320, 256), nullptr, 0,
                                           D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format, dpi, dpi),
                                           target_.GetAddressOf())));
        REQUIRE(SUCCEEDED(dc->CreateBitmap(
            D2D1::SizeU(320, 256), nullptr, 0,
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format, dpi, dpi),
            readback_.GetAddressOf())));
    }

    template <typename Draw> std::vector<uint8_t> Render(Draw &&draw)
    {
        ComPtr<ID2D1Image> previous;
        dc_->GetTarget(previous.GetAddressOf());
        dc_->SetTarget(target_.Get());
        dc_->BeginDraw();
        dc_->Clear(D2D1::ColorF(0, 0.0f));
        draw();
        REQUIRE(SUCCEEDED(dc_->EndDraw()));
        dc_->SetTarget(previous.Get());
        REQUIRE(SUCCEEDED(readback_->CopyFromBitmap(nullptr, target_.Get(), nullptr)));
        D2D1_MAPPED_RECT mapped{};
        REQUIRE(SUCCEEDED(readback_->Map(D2D1_MAP_OPTIONS_READ, &mapped)));
        std::vector<uint8_t> pixels(320 * 256 * 4);
        for (UINT row = 0; row < 256; ++row)
        {
            std::copy_n(mapped.bits + static_cast<size_t>(row) * mapped.pitch, 320 * 4,
                        pixels.begin() + static_cast<size_t>(row) * 320 * 4);
        }
        readback_->Unmap();
        return pixels;
    }

  private:
    ID2D1DeviceContext *dc_;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<ID2D1Bitmap1> readback_;
};

void CheckImagePixels(bool nativeSize)
{
    ImageFixture fixture;
    msimeui::DeviceResources resources;
    REQUIRE(resources.EnsureForComposition(fixture.hwnd));
    ID2D1DeviceContext *dc = resources.GetDeviceContext();
    ID2D1Bitmap *bitmap = resources.GetBitmapFromFile(fixture.path);
    REQUIRE(bitmap != nullptr);
    REQUIRE(bitmap->GetPixelSize().width == 132);
    REQUIRE(bitmap->GetPixelSize().height == 106);
    msimeui::Image image(fixture.path);
    int worst = 0;
    for (const float dpi : {96.0f, 120.0f, 144.0f, 192.0f, 96.0f})
    {
        dc->SetDpi(dpi, dpi);
        float dpiX = 0.0f;
        float dpiY = 0.0f;
        dc->GetDpi(&dpiX, &dpiY);
        REQUIRE_NEAR(dpiX, dpi);
        REQUIRE_NEAR(dpiY, dpi);
        Canvas canvas(dc, dpi);
        const float dipPerPixel = 96.0f / dpi;
        for (const float resize : nativeSize ? std::vector<float>{1.0f} : std::vector<float>{0.75f, 1.25f, 2.0f})
        {
            for (const float offset : {0.25f, 0.5f, 0.75f})
            {
                const msimeui::RectF bounds{(8.0f + offset) * dipPerPixel, (10.0f + offset) * dipPerPixel,
                                            132.0f * resize * dipPerPixel, 106.0f * resize * dipPerPixel};
                image.Arrange(bounds);
                const auto actual = canvas.Render([&]() { image.Render(resources); });
                const float x = (nativeSize ? std::round(8.0f + offset) : 8.0f + offset) * dipPerPixel;
                const float y = (nativeSize ? std::round(10.0f + offset) : 10.0f + offset) * dipPerPixel;
                const auto expected = canvas.Render([&]() {
                    resources.GetRenderTarget()->DrawBitmap(bitmap,
                                                            D2D1::RectF(x, y, x + bounds.width, y + bounds.height),
                                                            1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                });
                int difference = 0;
                for (size_t i = 0; i < actual.size(); ++i)
                {
                    difference = (std::max)(difference, std::abs(static_cast<int>(actual[i]) - expected[i]));
                }
                worst = (std::max)(worst, difference);
                if (difference > 1)
                {
                    std::printf("image mismatch dpi=%.0f resize=%.2f offset=%.2f max=%d\n", dpi, resize, offset,
                                difference);
                }
            }
        }
    }
    std::printf("%s image worst channel difference %d/255\n", nativeSize ? "native" : "scaled", worst);
    REQUIRE(worst <= 1);
}
} // namespace

TEST_CASE(native_size_image_keeps_pixel_detail_at_fractional_origins_and_dpi_changes)
{
    CheckImagePixels(true);
}

TEST_CASE(scaled_image_preserves_linear_interpolation)
{
    CheckImagePixels(false);
}
