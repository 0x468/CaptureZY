#include "feature_capture/screen_capture.h"

#include <chrono>
#include <format>
#include <utility>
#include <wincodec.h>
#include <wrl/client.h>

#include "core/log.h"
#include "feature_capture/capture_result.h"

namespace capturezy::feature_capture
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        [[nodiscard]] int RectWidth(RECT rect) noexcept
        {
            return rect.right - rect.left;
        }

        [[nodiscard]] int RectHeight(RECT rect) noexcept
        {
            return rect.bottom - rect.top;
        }

        void LogHResultFailure(wchar_t const *stage, HRESULT result) noexcept
        {
            try
            {
                CAPTUREZY_LOG_ERROR(core::LogCategory::FileIO,
                                    std::format(L"{} failed. hr=0x{:08X}.", stage, static_cast<unsigned long>(result)));
            }
            catch (...)
            {
                CAPTUREZY_LOG_ERROR(core::LogCategory::FileIO, L"File I/O HRESULT failure.");
            }
        }

        [[nodiscard]] bool WriteBitmapToPng(IWICImagingFactory *imaging_factory, HBITMAP bitmap, SIZE bitmap_size,
                                            wchar_t const *file_path) noexcept
        {
            ComPtr<IWICBitmap> wic_bitmap;
            HRESULT result = imaging_factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapUseAlpha,
                                                                      wic_bitmap.GetAddressOf());
            if (FAILED(result))
            {
                LogHResultFailure(L"CreateBitmapFromHBITMAP", result);
                return false;
            }

            ComPtr<IWICStream> stream;
            result = imaging_factory->CreateStream(stream.GetAddressOf());
            if (FAILED(result))
            {
                LogHResultFailure(L"CreateStream", result);
                return false;
            }

            result = stream->InitializeFromFilename(file_path, GENERIC_WRITE);
            if (FAILED(result))
            {
                LogHResultFailure(L"InitializeFromFilename", result);
                return false;
            }

            ComPtr<IWICBitmapEncoder> encoder;
            result = imaging_factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
            if (FAILED(result))
            {
                LogHResultFailure(L"CreateEncoder", result);
                return false;
            }

            result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
            if (FAILED(result))
            {
                LogHResultFailure(L"Encoder Initialize", result);
                return false;
            }

            ComPtr<IWICBitmapFrameEncode> frame;
            ComPtr<IPropertyBag2> property_bag;
            result = encoder->CreateNewFrame(frame.GetAddressOf(), property_bag.GetAddressOf());
            if (FAILED(result))
            {
                LogHResultFailure(L"CreateNewFrame", result);
                return false;
            }

            result = frame->Initialize(property_bag.Get());
            if (FAILED(result))
            {
                LogHResultFailure(L"Frame Initialize", result);
                return false;
            }

            result = frame->SetSize(static_cast<UINT>(bitmap_size.cx), static_cast<UINT>(bitmap_size.cy));
            if (FAILED(result))
            {
                LogHResultFailure(L"Frame SetSize", result);
                return false;
            }

            WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
            result = frame->SetPixelFormat(&pixel_format);
            if (FAILED(result))
            {
                LogHResultFailure(L"Frame SetPixelFormat", result);
                return false;
            }

            result = frame->WriteSource(wic_bitmap.Get(), nullptr);
            if (FAILED(result))
            {
                LogHResultFailure(L"Frame WriteSource", result);
                return false;
            }

            result = frame->Commit();
            if (FAILED(result))
            {
                LogHResultFailure(L"Frame Commit", result);
                return false;
            }

            result = encoder->Commit();
            if (FAILED(result))
            {
                LogHResultFailure(L"Encoder Commit", result);
                return false;
            }

            return true;
        }
    } // namespace

    CapturedBitmap::CapturedBitmap(HBITMAP bitmap, SIZE size) noexcept : bitmap_(bitmap), size_(size) {}

    CapturedBitmap::~CapturedBitmap() noexcept
    {
        if (bitmap_ != nullptr)
        {
            DeleteObject(bitmap_);
        }
    }

    CapturedBitmap::CapturedBitmap(CapturedBitmap &&other) noexcept
        : bitmap_(std::exchange(other.bitmap_, nullptr)), size_(std::exchange(other.size_, SIZE{}))
    {
    }

    CapturedBitmap &CapturedBitmap::operator=(CapturedBitmap &&other) noexcept
    {
        if (this != &other)
        {
            if (bitmap_ != nullptr)
            {
                DeleteObject(bitmap_);
            }

            bitmap_ = std::exchange(other.bitmap_, nullptr);
            size_ = std::exchange(other.size_, SIZE{});
        }

        return *this;
    }

    bool CapturedBitmap::IsValid() const noexcept
    {
        return bitmap_ != nullptr;
    }

    HBITMAP CapturedBitmap::Get() const noexcept
    {
        return bitmap_;
    }

    HBITMAP CapturedBitmap::Release() noexcept
    {
        size_ = {};
        return std::exchange(bitmap_, nullptr);
    }

    SIZE CapturedBitmap::Size() const noexcept
    {
        return size_;
    }

    CapturedBitmap CapturedBitmap::Clone() const noexcept
    {
        if (bitmap_ == nullptr)
        {
            return {};
        }

        HDC screen_device_context = GetDC(nullptr);
        if (screen_device_context == nullptr)
        {
            return {};
        }

        HDC source_device_context = CreateCompatibleDC(screen_device_context);
        HDC destination_device_context = CreateCompatibleDC(screen_device_context);
        if (source_device_context == nullptr || destination_device_context == nullptr)
        {
            if (destination_device_context != nullptr)
            {
                DeleteDC(destination_device_context);
            }
            if (source_device_context != nullptr)
            {
                DeleteDC(source_device_context);
            }
            ReleaseDC(nullptr, screen_device_context);
            return {};
        }

        HBITMAP cloned_bitmap = CreateCompatibleBitmap(screen_device_context, size_.cx, size_.cy);
        if (cloned_bitmap == nullptr)
        {
            DeleteDC(destination_device_context);
            DeleteDC(source_device_context);
            ReleaseDC(nullptr, screen_device_context);
            return {};
        }

        HGDIOBJ previous_source_bitmap = SelectObject(source_device_context, bitmap_);
        HGDIOBJ previous_destination_bitmap = SelectObject(destination_device_context, cloned_bitmap);
        bool const copied = BitBlt(destination_device_context, 0, 0, size_.cx, size_.cy, source_device_context, 0, 0,
                                   SRCCOPY) != FALSE;

        SelectObject(destination_device_context, previous_destination_bitmap);
        SelectObject(source_device_context, previous_source_bitmap);
        DeleteDC(destination_device_context);
        DeleteDC(source_device_context);
        ReleaseDC(nullptr, screen_device_context);

        if (!copied)
        {
            DeleteObject(cloned_bitmap);
            return {};
        }

        return {cloned_bitmap, size_};
    }

    CaptureResult ScreenCapture::CaptureRegion(RECT screen_rect) noexcept
    {
        int const width = RectWidth(screen_rect);
        int const height = RectHeight(screen_rect);

        if (width <= 0 || height <= 0)
        {
            return {};
        }

        HDC screen_device_context = GetDC(nullptr);
        if (screen_device_context == nullptr)
        {
            return {};
        }

        HDC memory_device_context = CreateCompatibleDC(screen_device_context);
        if (memory_device_context == nullptr)
        {
            ReleaseDC(nullptr, screen_device_context);
            return {};
        }

        HBITMAP bitmap = CreateCompatibleBitmap(screen_device_context, width, height);
        if (bitmap == nullptr)
        {
            DeleteDC(memory_device_context);
            ReleaseDC(nullptr, screen_device_context);
            return {};
        }

        HGDIOBJ previous_bitmap = SelectObject(memory_device_context, bitmap);
        bool const copied = BitBlt(memory_device_context, 0, 0, width, height, screen_device_context, screen_rect.left,
                                   screen_rect.top, SRCCOPY | CAPTUREBLT) != FALSE;

        SelectObject(memory_device_context, previous_bitmap);
        DeleteDC(memory_device_context);
        ReleaseDC(nullptr, screen_device_context);

        if (!copied)
        {
            DeleteObject(bitmap);
            return {};
        }

        return {CapturedBitmap(bitmap, SIZE{.cx = width, .cy = height}), screen_rect, std::chrono::system_clock::now()};
    }

    bool ScreenCapture::CopyBitmapToClipboard(HWND owner_window, CaptureResult const &capture_result) noexcept
    {
        if (!capture_result.IsValid())
        {
            CAPTUREZY_LOG_WARNING(core::LogCategory::Clipboard,
                                  L"Skip clipboard copy because capture result is invalid.");
            return false;
        }

        if (OpenClipboard(owner_window) == FALSE)
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Clipboard, L"OpenClipboard failed.");
            return false;
        }

        if (EmptyClipboard() == FALSE)
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Clipboard, L"EmptyClipboard failed.");
            CloseClipboard();
            return false;
        }

        CapturedBitmap bitmap = capture_result.CloneBitmap();
        if (!bitmap.IsValid())
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Clipboard, L"Failed to clone bitmap for clipboard copy.");
            CloseClipboard();
            return false;
        }

        HBITMAP const clipboard_bitmap = bitmap.Release();
        if (SetClipboardData(CF_BITMAP, clipboard_bitmap) == nullptr)
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Clipboard, L"SetClipboardData failed.");
            CloseClipboard();
            DeleteObject(clipboard_bitmap);
            return false;
        }

        CloseClipboard();
        CAPTUREZY_LOG_INFO(core::LogCategory::Clipboard, L"Copied capture bitmap to clipboard.");
        return true;
    }

    bool ScreenCapture::SaveBitmapToPng(CaptureResult const &capture_result, wchar_t const *file_path) noexcept
    {
        if (!capture_result.IsValid() || file_path == nullptr || *file_path == L'\0')
        {
            CAPTUREZY_LOG_WARNING(core::LogCategory::FileIO,
                                  L"Skip PNG save because capture result or file path is invalid.");
            return false;
        }

        ComPtr<IWICImagingFactory> imaging_factory;
        HRESULT const factory_result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                        IID_PPV_ARGS(imaging_factory.GetAddressOf()));
        if (FAILED(factory_result) || imaging_factory == nullptr)
        {
            LogHResultFailure(L"CoCreateInstance(CLSID_WICImagingFactory)", factory_result);
            return false;
        }

        return WriteBitmapToPng(imaging_factory.Get(), capture_result.Bitmap().Get(), capture_result.PixelSize(),
                                file_path);
    }
} // namespace capturezy::feature_capture
