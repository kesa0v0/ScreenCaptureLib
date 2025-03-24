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
#include <chrono>
#include <timeapi.h>
#include <windows.h>

#pragma comment(lib, "winmm.lib") // 📌 winmm 라이브러리 링크 추가
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
double frameTime = 1000 / targetFPS;
std::vector<unsigned char> frameBuffer(frameWidth* frameHeight * 4, 0);


// DLL 로드 테스트 함수
extern "C" __declspec(dllexport) const char* TestDLL() {
	return "DLL is successfully loaded!";
}

// 정밀한 타이머 설정 (Windows 전용)
void SetHighPrecisionTimer() {
	timeBeginPeriod(1); // Windows 타이머 해상도를 1ms로 설정 (더 정확한 타이밍 가능)
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
void CaptureLoop(void (*frameCallback)(FrameData frameData)) {
	HRESULT hr;
	try {
		SetHighPrecisionTimer();

		while (capturing) {
			ComPtr<IDXGIResource> desktopResource;
			DXGI_OUTDUPL_FRAME_INFO frameInfo;
			ComPtr<ID3D11Texture2D> acquiredTexture;

			auto startTime = std::chrono::high_resolution_clock::now();
			auto startEpochTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

			// 새 프레임 가져오기
			hr = desktopDuplication->AcquireNextFrame(33, &frameInfo, &desktopResource);
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


			// 콜백용 프레임 데이터 생성
			FrameData frameData;
			frameData.data = frameBuffer.data();
			frameData.width = frameWidth;
			frameData.height = frameHeight;
			frameData.timeStamp = startEpochTime;

			// 콜백 호출 (프레임 데이터 전달)
			frameCallback(frameData);


			while (true) {
				auto now = std::chrono::high_resolution_clock::now();
				double elapsedTime = std::chrono::duration<double, std::milli>(now - startTime).count();
				if (elapsedTime >= frameTime) {
					break; // 목표 시간 도달 시 다음 프레임 진행
				}
			}
		}
		timeEndPeriod(1);
	}
	catch (std::exception& e) {
		printf("e");
	}
}

// 캡처 시작
extern "C" __declspec(dllexport) void StartCapture(void (*frameCallback)(FrameData frameData)) {
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
