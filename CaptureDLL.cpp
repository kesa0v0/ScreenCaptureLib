#include "pch.h"
#include "CaptureDLL.h"
#include "lz4.h"
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
#include <cstring>


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

void logError(const std::string& errorMessage) {
	// 현재 시간을 가져오기
	auto now = std::chrono::system_clock::now();    
	auto time = std::chrono::current_zone()->to_local(now); // 지역 시간으로 변환

	// 시간 포맷 설정 (시:분:초)
	std::string timeStr = std::format("{:%H:%M:%S}", time);

	// 로그 메시지 생성   
	std::string logMessage = std::format("[{}] ERROR: {}", timeStr, errorMessage);

	// 콘솔 출력
	std::cerr << logMessage << std::endl;
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
		logError("Failed to create D3D11 device");
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
		logError("Failed to initialize output duplication");
		return false;
	}

	std::cout << "Capture initialized" << std::endl;
	return true;
}


// 새 프레임 캡처
bool AcquireFrame(ComPtr<ID3D11Texture2D>& acquiredTexture) {
	ComPtr<IDXGIResource> desktopResource;
	DXGI_OUTDUPL_FRAME_INFO frameInfo;

	HRESULT hr = desktopDuplication->AcquireNextFrame(16, &frameInfo, &desktopResource);
	if (hr != S_OK) {
		if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
		if (hr == DXGI_ERROR_ACCESS_LOST) {
			logError("Access lost");
			return false;
		}
		logError("Failed to acquire frame");
		std::cerr << "Error code: " << hr << std::endl;
		return false;
	}

	desktopResource.As(&acquiredTexture);
	return true;
}

// CPU 텍스처 생성 및 데이터 복사
bool MapFrameToCPU(ComPtr<ID3D11Texture2D>& acquiredTexture, std::vector<unsigned char>& frameBuffer) {
	D3D11_TEXTURE2D_DESC textureDesc;
	acquiredTexture->GetDesc(&textureDesc);
	textureDesc.Usage = D3D11_USAGE_STAGING;
	textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	textureDesc.BindFlags = 0;

	ComPtr<ID3D11Texture2D> cpuTexture;
	HRESULT hr = d3dDevice->CreateTexture2D(&textureDesc, nullptr, &cpuTexture);
	if (FAILED(hr)) {
		logError("Failed to create staging texture");
		return false;
	}

	d3dContext->CopyResource(cpuTexture.Get(), acquiredTexture.Get());

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	hr = d3dContext->Map(cpuTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
	if (FAILED(hr)) {
		logError("Failed to map texture");
		return false;
	}

	unsigned char* srcData = static_cast<unsigned char*>(mappedResource.pData);
	int rowPitch = mappedResource.RowPitch;

	for (int y = 0; y < frameHeight; ++y) {
		memcpy(&frameBuffer[y * frameWidth * 4], &srcData[y * rowPitch], frameWidth * 4);
	}

	d3dContext->Unmap(cpuTexture.Get(), 0);
	return true;
}

// LZ4 압축 수행
int CompressFrame(const std::vector<unsigned char>& frameBuffer, std::vector<unsigned char>& compressedBuffer) {
	int compressedSize = LZ4_compress_default(
		reinterpret_cast<const char*>(frameBuffer.data()),       // 원본 데이터
		reinterpret_cast<char*>(compressedBuffer.data()),        // 압축 버퍼
		frameWidth * frameHeight * 4,                           // 원본 크기
		LZ4_compressBound(frameWidth * frameHeight * 4));       // 최대 압축 크기

	if (compressedSize <= 0) {
		logError("Compression failed");
		return -1;
	}

	return compressedSize;
}

// 메인 캡처 루프
void CaptureLoop(void (*frameCallback)(FrameData frameData)) {
	try {
		while (capturing) {
			ComPtr<ID3D11Texture2D> acquiredTexture;

			// 프레임 유지, 지연 시간 측정을 위한 타임스탬프
			auto startTime = std::chrono::high_resolution_clock::now();
			auto startEpochTime = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch())
				.count();

			// 새 프레임 가져오기
			if (!AcquireFrame(acquiredTexture)) {
				continue;
			}

			// 프레임 데이터를 CPU로 가져오기
			if (!MapFrameToCPU(acquiredTexture, frameBuffer)) {
				continue;
			}

			// LZ4 압축 수행
			std::vector<unsigned char> compressedBuffer(frameWidth * frameHeight * 4);
			int compressedSize = CompressFrame(frameBuffer, compressedBuffer);
			if (compressedSize <= 0) {
				continue;
			}

			// 콜백 전달
			FrameData frameData;
			frameData.data = compressedBuffer;
			frameData.size = compressedSize;
			frameData.width = frameWidth;
			frameData.height = frameHeight;
			frameData.timeStamp = startEpochTime;

			frameCallback(frameData);

			// 프레임 주기를 유지
			while (true) {
				auto now = std::chrono::high_resolution_clock::now();
				double elapsedTime = std::chrono::duration<double, std::milli>(now - startTime).count();
				if (elapsedTime >= frameTime) {
					break;
				}
			}
		}
	}
	catch (std::exception& e) {
		logError(e.what());
	}
}



// 캡처 시작
extern "C" __declspec(dllexport) void StartCapture(void (*frameCallback)(FrameData frameData)) {
	std::lock_guard<std::mutex> lock(captureMutex);
	if (!capturing) {
		if (!InitializeCapture()) {
			capturing = false;
			logError("Capture initialization failed!");
			return;
		}

		capturing = true;
		captureThread = std::thread(CaptureLoop, frameCallback);
	}
}

// 캡처 중지
extern "C" __declspec(dllexport) void StopCapture() {
	std::cout << "Stopping capture" << std::endl;

	std::lock_guard<std::mutex> lock(captureMutex);
	capturing = false;

	if (captureThread.joinable()) {
		try {
			captureThread.join();
		}
		catch (...) {
			logError("Error while joining captureThread");
		}
	}

	// DirectX 자원 해제
	if (desktopDuplication) {
		desktopDuplication->Release();
		desktopDuplication = nullptr;
	}
	if (d3dContext) {
		d3dContext->Release();
		d3dContext = nullptr;
	}
	if (d3dDevice) {
		d3dDevice->Release();
		d3dDevice = nullptr;
	}
}

