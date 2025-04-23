#include "CaptureManager.h"
#include "Log.h"
#include "lz4/lz4.h"

#include <chrono>
#include <iostream>
#include <immintrin.h>

#define NOFRAMECHANGE 1557

using namespace Microsoft::WRL;

CaptureManager::CaptureManager()
    : capturing(false), threadPool(std::thread::hardware_concurrency()) {
}

CaptureManager::~CaptureManager() {
    Stop();
}

bool CaptureManager::Initialize(int width, int height, int fps) {
    frameWidth = width;
    frameHeight = height;
    targetFPS = fps;
    frameTime = 1000.0 / targetFPS;
    frameSize = frameWidth * frameHeight * 4;

    previousFrameBuffer.resize(frameSize, 0);
    frameBuffer.resize(frameSize, 0);

    return InitializeDirectX();
}

bool CaptureManager::InitializeDirectX() {
    HRESULT hr;
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    d3dDevice.As(&dxgiDevice);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);
    ComPtr<IDXGIOutput> output;
    adapter->EnumOutputs(0, &output);
    ComPtr<IDXGIOutput1> output1;
    output.As(&output1);
    hr = output1->DuplicateOutput(d3dDevice.Get(), &desktopDuplication);

    return SUCCEEDED(hr);
}

void CaptureManager::Start(std::function<void(FrameData)> callback) {
    std::lock_guard<std::mutex> lock(captureMutex);
    if (capturing) return;

    frameCallback = callback;
    capturing = true;
    captureThread = std::thread(&CaptureManager::CaptureLoop, this);
}

void CaptureManager::Stop() {
    std::lock_guard<std::mutex> lock(captureMutex);
    capturing = false;

    if (captureThread.joinable()) {
        captureThread.join();
    }

    desktopDuplication.Reset();
    d3dContext.Reset();
    d3dDevice.Reset();
}

bool CaptureManager::IsCapturing() const {
    return capturing;
}

void CaptureManager::CaptureLoop() {
    while (capturing) {
        ComPtr<IDXGIResource> desktopResource;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        ComPtr<ID3D11Texture2D> acquiredTexture;

        int result = AcquireFrame(frameInfo, desktopResource);
        if (result == 0 || !capturing) continue;

        if (result != NOFRAMECHANGE) {
            if (!MapFrameToCPU(desktopResource, acquiredTexture) || !capturing) continue;
        }
        else {
            frameBuffer = previousFrameBuffer;
        }

        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        threadPool.enqueueTask([=]() {
            FrameData frameData;
            frameData.data = frameBuffer.data();
            frameData.width = frameWidth;
            frameData.height = frameHeight;
            frameData.frameRate = targetFPS;
            frameData.dataSize = frameBuffer.size();
            frameData.timeStamp = timestamp;
            frameCallback(frameData);
            });

        previousFrameBuffer = frameBuffer;
        std::this_thread::sleep_for(std::chrono::milliseconds((int)frameTime));
    }
}

int CaptureManager::AcquireFrame(DXGI_OUTDUPL_FRAME_INFO& frameInfo, ComPtr<IDXGIResource>& desktopResource) {
    HRESULT hr = desktopDuplication->AcquireNextFrame(16, &frameInfo, &desktopResource);
    switch (hr) {
    case DXGI_ERROR_ACCESS_LOST: return false;
    case DXGI_ERROR_WAIT_TIMEOUT: return NOFRAMECHANGE;
    case DXGI_ERROR_INVALID_CALL: return false;
    case S_OK: return true;
    default: return false;
    }
}

bool CaptureManager::MapFrameToCPU(ComPtr<IDXGIResource>& desktopResource, ComPtr<ID3D11Texture2D>& acquiredTexture) {
    desktopResource.As(&acquiredTexture);
    D3D11_TEXTURE2D_DESC desc;
    acquiredTexture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> cpuTexture;
    if (FAILED(d3dDevice->CreateTexture2D(&desc, nullptr, &cpuTexture))) return false;

    d3dContext->CopyResource(cpuTexture.Get(), acquiredTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(d3dContext->Map(cpuTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;

    uint8_t* src = reinterpret_cast<uint8_t*>(mapped.pData);
    for (int y = 0; y < frameHeight; ++y) {
        memcpy(&frameBuffer[y * frameWidth * 4], &src[y * mapped.RowPitch], frameWidth * 4);
    }

    d3dContext->Unmap(cpuTexture.Get(), 0);
    desktopDuplication->ReleaseFrame();
    return true;
}
