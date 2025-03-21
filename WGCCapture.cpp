#include <windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <wrl.h>
#include <iostream>
#include <fstream>
#include <windows.graphics.capture.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Foundation.h>
#include <windows.graphics.directx.direct3d11.interop.h>

using namespace Microsoft::WRL;
using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

// 캡처 기능을 수행하는 클래스
class ScreenCaptureWGC
{
public:
    ScreenCaptureWGC() : d3dDevice(nullptr) {}

    bool Initialize(HWND hwnd);
    bool CaptureFrame();
    void SaveBitmap(const std::wstring &filename, BYTE *data, int width, int height);
    ~ScreenCaptureWGC();

private:
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    GraphicsCaptureItem captureItem{nullptr};
    GraphicsCaptureSession captureSession{nullptr};
    Direct3D11CaptureFramePool framePool{nullptr};
    int screenWidth;
    int screenHeight;
};

bool ScreenCaptureWGC::Initialize(HWND hwnd)
{
    HRESULT hr;

    // Windows Runtime 초기화
    winrt::init_apartment();

    // Direct3D 11 장치 생성
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
        D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext);
    if (FAILED(hr))
    {
        std::cerr << "Failed to create D3D11 device." << std::endl;
        return false;
    }

    // 특정 창을 캡처할 GraphicsCaptureItem 생성
    auto windowId = winrt::Windows::UI::WindowId{reinterpret_cast<uint64_t>(hwnd)};
    captureItem = GraphicsCaptureItem::TryCreateFromWindowId(windowId);
    if (!captureItem)
    {
        std::cerr << "Failed to create GraphicsCaptureItem." << std::endl;
        return false;
    }

    // 캡처 대상 크기 설정
    screenWidth = captureItem.Size().Width;
    screenHeight = captureItem.Size().Height;

    // DXGI 인터페이스 변환
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = d3dDevice.As(&dxgiDevice); // ID3D11Device → IDXGIDevice 변환
    if (FAILED(hr))
    {
        std::cerr << "Failed to get IDXGIDevice from ID3D11Device." << std::endl;
        return false;
    }

    // Windows Runtime Direct3D 장치 생성
    winrt::com_ptr<winrt::Windows::Foundation::IInspectable> dxgiInteropDevice;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), reinterpret_cast<IInspectable **>(dxgiInteropDevice.put()));
    if (FAILED(hr))
    {
        std::cerr << "Failed to create Direct3D device for Windows Graphics Capture." << std::endl;
        return false;
    }

    // GraphicsCaptureSession 생성 (dxgiInteropDevice를 IDirect3DDevice로 변환하여 사용)
    auto direct3DDevice = dxgiInteropDevice.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

    framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
                    direct3DDevice, // 변환된 장치 사용
                    winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                    2, captureItem.Size());

    captureSession = framePool.CreateCaptureSession(captureItem);
    captureSession.StartCapture();
    captureSession.StartCapture();

    return true;
}

bool ScreenCaptureWGC::CaptureFrame()
{
    if (!captureSession)
        return false;

    auto frame = framePool.TryGetNextFrame();
    if (!frame)
    {
        std::cerr << "Failed to capture frame." << std::endl;
        return false;
    }

    auto surface = frame.Surface();
    auto texture = surface.as<ID3D11Texture2D>();

    // CPU에서 읽을 수 있는 스테이징 텍스처 생성
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> stagingTexture;
    HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &stagingTexture);
    if (FAILED(hr))
    {
        std::cerr << "Failed to create staging texture." << std::endl;
        return false;
    }

    // GPU -> CPU로 데이터 복사
    d3dContext->CopyResource(stagingTexture.Get(), texture.get());

    // CPU에서 읽을 수 있도록 매핑
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = d3dContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr))
    {
        std::cerr << "Failed to map texture." << std::endl;
        return false;
    }

    // BMP 저장
    SaveBitmap(L"screenshot_wgc.bmp", (BYTE *)mappedResource.pData, screenWidth, screenHeight);

    // 매핑 해제
    d3dContext->Unmap(stagingTexture.Get(), 0);

    return true;
}

void ScreenCaptureWGC::SaveBitmap(const std::wstring &filename, BYTE *data, int width, int height)
{
    std::ofstream file(filename, std::ios::binary);

    if (!file)
    {
        std::cerr << "Failed to open file for writing." << std::endl;
        return;
    }

    BITMAPFILEHEADER fileHeader = {};
    BITMAPINFOHEADER infoHeader = {};

    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + (width * height * 4);

    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = width;
    infoHeader.biHeight = -height; // Top-down BMP
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = width * height * 4;

    file.write(reinterpret_cast<const char *>(&fileHeader), sizeof(fileHeader));
    file.write(reinterpret_cast<const char *>(&infoHeader), sizeof(infoHeader));
    file.write(reinterpret_cast<const char *>(data), width * height * 4);

    file.close();
}

ScreenCaptureWGC::~ScreenCaptureWGC()
{
    captureSession.Close();
}

int main()
{
    // 캡처할 창의 핸들 (현재 실행 중인 윈도우 중 하나 선택)
    HWND hwnd = FindWindow(nullptr, L"제목을 입력하세요"); // 캡처할 창의 제목을 입력해야 합니다.

    if (!hwnd)
    {
        std::cerr << "Failed to find window." << std::endl;
        return -1;
    }

    ScreenCaptureWGC capture;

    if (!capture.Initialize(hwnd))
    {
        std::cerr << "Failed to initialize screen capture." << std::endl;
        return -1;
    }

    std::cout << "Capturing screen..." << std::endl;
    if (capture.CaptureFrame())
    {
        std::cout << "Screenshot saved as screenshot_wgc.bmp" << std::endl;
    }
    else
    {
        std::cerr << "Failed to capture screen." << std::endl;
    }

    return 0;
}
