#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl.h>
#include <vector>
#include <thread>
#include <mutex>
#include <functional>
#include <atomic>

#include "ThreadPool.h"
#include "FrameData.h"

class CaptureManager {
public:
    CaptureManager();
    ~CaptureManager();

    bool Initialize(int width, int height, int fps);
    void Start(std::function<void(FrameData)> callback);
    void Stop();
    bool IsCapturing() const;

private:
    void CaptureLoop();
    bool InitializeDirectX();
    int AcquireFrame(DXGI_OUTDUPL_FRAME_INFO& frameInfo, Microsoft::WRL::ComPtr<IDXGIResource>& desktopResource);
    bool MapFrameToCPU(Microsoft::WRL::ComPtr<IDXGIResource>& desktopResource, Microsoft::WRL::ComPtr<ID3D11Texture2D>& acquiredTexture);
    void CompressFrame(const uint8_t* currentFrame, const uint8_t* previousFrame, std::vector<unsigned char>& compressedData);
    void CalculateDiffSIMD(const uint8_t* currentFrame, const uint8_t* previousFrame, uint8_t* diffBuffer, size_t frameSize);

    std::atomic<bool> capturing;
    std::thread captureThread;
    std::mutex captureMutex;

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> desktopDuplication;

    int frameWidth;
    int frameHeight;
    int frameSize;
    int targetFPS;
    double frameTime;

    std::vector<unsigned char> previousFrameBuffer;
    std::vector<unsigned char> frameBuffer;

    ThreadPool threadPool;
    std::function<void(FrameData)> frameCallback;
};
