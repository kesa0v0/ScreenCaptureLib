#include "pch.h"
#include "CaptureDLL.h"
#include <windows.graphics.capture.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl.h>
#include <thread>
#include <vector>
#include <mutex>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Microsoft::WRL;

// ���� ����
bool capturing = false;
std::thread captureThread;
std::mutex captureMutex;

// DirectX ����
ComPtr<ID3D11Device> d3dDevice;
ComPtr<ID3D11DeviceContext> d3dContext;
ComPtr<IDXGIOutputDuplication> desktopDuplication;

// �ػ� �� �����ӹ���
int frameWidth = 1920;
int frameHeight = 1080;
std::vector<unsigned char> frameBuffer(frameWidth* frameHeight * 4, 0);

// DLL �ε� �׽�Ʈ �Լ�
extern "C" __declspec(dllexport) const char* TestDLL() {
    return "DLL is successfully loaded!";
}

// DirectX 11 �ʱ�ȭ �Լ�
bool InitializeCapture() {
    HRESULT hr;

    // DirectX 11 ��ġ ����
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext
    );

    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D11 device\n";
        return false;
    }

    // DXGI Factory �� ����� ��������
    ComPtr<IDXGIDevice> dxgiDevice;
    d3dDevice.As(&dxgiDevice);

    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);

    ComPtr<IDXGIOutput> output;
    adapter->EnumOutputs(0, &output);

    ComPtr<IDXGIOutput1> output1;
    output.As(&output1);

    // Output Duplication �ʱ�ȭ
    hr = output1->DuplicateOutput(d3dDevice.Get(), &desktopDuplication);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize output duplication\n";
        return false;
    }

    return true;
}

// ĸó ����
void CaptureLoop(void (*frameCallback)(unsigned char* data, int width, int height)) {
    HRESULT hr;
    try {

    while (capturing) {
        ComPtr<IDXGIResource> desktopResource;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        ComPtr<ID3D11Texture2D> acquiredTexture;

        // �� ������ ��������
        hr = desktopDuplication->AcquireNextFrame(16, &frameInfo, &desktopResource);
        if (FAILED(hr)) {
            std::cerr << "Failed to acquire frame\n";
            continue;
        }

        // 2D �ؽ�ó ��������
        desktopResource.As(&acquiredTexture);

        // CPU ���� ������ �ؽ�ó ����
        D3D11_TEXTURE2D_DESC textureDesc;
        acquiredTexture->GetDesc(&textureDesc);
        textureDesc.Usage = D3D11_USAGE_STAGING;
        textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        textureDesc.BindFlags = 0;
        textureDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> cpuTexture;
        hr = d3dDevice->CreateTexture2D(&textureDesc, nullptr, &cpuTexture);
        if (FAILED(hr)) {
            std::cerr << "Failed to create staging texture\n";
            desktopDuplication->ReleaseFrame();
            continue;
        }

        // GPU -> CPU ����
        d3dContext->CopyResource(cpuTexture.Get(), acquiredTexture.Get());

        // �����Ͽ� ������ ��������
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = d3dContext->Map(cpuTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
        if (FAILED(hr)) {
            std::cerr << "Failed to map texture\n";
            desktopDuplication->ReleaseFrame();
            continue;
        }

        // �����͸� frameBuffer�� ����
        unsigned char* srcData = static_cast<unsigned char*>(mappedResource.pData);
        int rowPitch = mappedResource.RowPitch;

        for (int y = 0; y < frameHeight; ++y) {
            memcpy(&frameBuffer[y * frameWidth * 4], &srcData[y * rowPitch], frameWidth * 4);
        }

        d3dContext->Unmap(cpuTexture.Get(), 0);
        desktopDuplication->ReleaseFrame();

        // �ݹ� ȣ�� (������ ������ ����)
        frameCallback(frameBuffer.data(), frameWidth, frameHeight);
    }
	}
    catch (std::exception& e) {
        printf("e");
    }
}

// ĸó ����
extern "C" __declspec(dllexport) void StartCapture(void (*frameCallback)(unsigned char* data, int width, int height)) {
    std::lock_guard<std::mutex> lock(captureMutex);
    if (!capturing) {
        if (!InitializeCapture()) {
            std::cerr << "Capture initialization failed!\n";
            return;
        }

        capturing = true;
        captureThread = std::thread(CaptureLoop, frameCallback);
    }
}

// ĸó ����
extern "C" __declspec(dllexport) void StopCapture() {
    {
        std::lock_guard<std::mutex> lock(captureMutex);
        capturing = false;
    }
    if (captureThread.joinable()) {
        captureThread.join();
    }
}
