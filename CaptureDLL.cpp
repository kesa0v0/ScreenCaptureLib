﻿#include "pch.h"
#include "CaptureDLL.h"
#include <windows.graphics.capture.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl.h>
#include <thread>
#include <vector>
#include <mutex>
#include <iostream>
#include <chrono>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Microsoft::WRL;


// 전역 변수
bool capturing = false;
std::thread captureThread;
std::mutex captureMutex;

// DirectX 변수
ComPtr<ID3D11Device> d3dDevice;
ComPtr<ID3D11DeviceContext> d3dContext;
ComPtr<IDXGIOutputDuplication> desktopDuplication;

// 해상도 및 프레임버퍼
int frameWidth = 1920;
int frameHeight = 1080;
int targetFPS = 60;
int frameTime = 1000 / targetFPS;
std::vector<unsigned char> frameBuffer(frameWidth* frameHeight * 4, 0);

// DLL 로드 테스트 함수
extern "C" __declspec(dllexport) const char* TestDLL() {
	return "DLL is successfully loaded!";
}

// DirectX 11 초기화 함수
bool InitializeCapture() {
	HRESULT hr;

	// DirectX 11 장치 생성
	D3D_FEATURE_LEVEL featureLevel;
	hr = D3D11CreateDevice(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		nullptr, 0, D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext
	);

	if (FAILED(hr)) {
		std::cerr << "Failed to create D3D11 device\n";
		return false;
	}

	// DXGI Factory 및 어댑터 가져오기
	ComPtr<IDXGIDevice> dxgiDevice;
	d3dDevice.As(&dxgiDevice);

	ComPtr<IDXGIAdapter> adapter;
	dxgiDevice->GetAdapter(&adapter);

	ComPtr<IDXGIOutput> output;
	adapter->EnumOutputs(0, &output);

	ComPtr<IDXGIOutput1> output1;
	output.As(&output1);

	// Output Duplication 초기화
	hr = output1->DuplicateOutput(d3dDevice.Get(), &desktopDuplication);
	if (FAILED(hr)) {
		std::cerr << "Failed to initialize output duplication\n";
		return false;
	}

	return true;
}

// 캡처 루프
void CaptureLoop(void (*frameCallback)(unsigned char* data, int width, int height)) {
	HRESULT hr;
	try {
		while (capturing) {
			ComPtr<IDXGIResource> desktopResource;
			DXGI_OUTDUPL_FRAME_INFO frameInfo;
			ComPtr<ID3D11Texture2D> acquiredTexture;

			auto startTime = std::chrono::high_resolution_clock::now();
			long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(startTime.time_since_epoch()).count();

			// 새 프레임 가져오기
			hr = desktopDuplication->AcquireNextFrame(16, &frameInfo, &desktopResource);
			if (FAILED(hr)) {
				std::cerr << "Failed to acquire frame\n";
				continue;
			}

			// 2D 텍스처 가져오기
			desktopResource.As(&acquiredTexture);

			// CPU 접근 가능한 텍스처 생성
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

			// GPU -> CPU 복사
			d3dContext->CopyResource(cpuTexture.Get(), acquiredTexture.Get());

			// 맵핑하여 데이터 가져오기
			D3D11_MAPPED_SUBRESOURCE mappedResource;
			hr = d3dContext->Map(cpuTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
			if (FAILED(hr)) {
				std::cerr << "Failed to map texture\n";
				desktopDuplication->ReleaseFrame();
				continue;
			}

			// 데이터를 frameBuffer로 복사
			unsigned char* srcData = static_cast<unsigned char*>(mappedResource.pData);
			int rowPitch = mappedResource.RowPitch;

			for (int y = 0; y < frameHeight; ++y) {
				memcpy(&frameBuffer[y * frameWidth * 4], &srcData[y * rowPitch], frameWidth * 4);
			}

			d3dContext->Unmap(cpuTexture.Get(), 0);
			desktopDuplication->ReleaseFrame();

			// 첫 8바이트에 타임스탬프 추가
			memcpy(frameBuffer.data(), &timestamp, sizeof(timestamp));

			// 콜백 호출 (프레임 데이터 전달)
			frameCallback(frameBuffer.data(), frameWidth, frameHeight);


			// FPS 제한 (프레임 간격 유지)
			auto endTime = std::chrono::high_resolution_clock::now();
			auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
			int sleepTime = frameTime - static_cast<int>(elapsedTime);

			if (sleepTime > 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
			}
		}
	}
	catch (std::exception& e) {
		printf("e");
	}
}

// 캡처 시작
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

// 캡처 중지
extern "C" __declspec(dllexport) void StopCapture() {
	{
		std::lock_guard<std::mutex> lock(captureMutex);
		capturing = false;
	}
	if (captureThread.joinable()) {
		captureThread.join();
	}
}
