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
#include <immintrin.h>
#include <queue>
#include <condition_variable>
#include <functional>

#include "Log.h"
#include "lz4/lz4.h"

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
int _frameWidth = 1920;
int _frameHeight = 1080;
int FRAME_SIZE = _frameWidth * _frameHeight * 4;
int _targetFPS = 60;
double frameTime = 1000 / _targetFPS;
std::vector<unsigned char> previousFrameBuffer(_frameWidth* _frameHeight * 4, 0); // 이전 프레임 버퍼
std::vector<unsigned char> frameBuffer(_frameWidth* _frameHeight * 4, 0); // 압축 버퍼


// DLL 로드 테스트 함수
extern "C" __declspec(dllexport) const char* TestDLL() {
	return "DLL is successfully loaded!";
}

// 쓰레드 풀 클래스 정의
class ThreadPool {
private:
	std::vector<std::thread> workers;
	std::queue<std::function<void()>> tasks;
	std::mutex queueMutex;
	std::condition_variable condition;
	bool stop;

public:
	explicit ThreadPool(size_t threadCount) : stop(false) {
		for (size_t i = 0; i < threadCount; ++i) {
			workers.emplace_back([this] {
				while (true) {
					std::function<void()> task;
					{
						std::unique_lock<std::mutex> lock(queueMutex);
						condition.wait(lock, [this] { return stop || !tasks.empty(); });
						if (stop && tasks.empty()) return;
						task = std::move(tasks.front());
						tasks.pop();
					}
					task();
				}
				});
		}
	}

	~ThreadPool() {
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			stop = true;
		}
		condition.notify_all();
		for (std::thread& worker : workers) {
			worker.join();
		}
	}

	void enqueueTask(std::function<void()> task) {
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			tasks.push(std::move(task));
		}
		condition.notify_one();
	}
};

// 전역 쓰레드 풀 인스턴스
ThreadPool pool(std::thread::hardware_concurrency()); // CPU 코어 수만큼 쓰레드 생성

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
		loge("Failed to initialize desktop duplication");
		return false;
	}

	// 모든 픽셀을 0으로 초기화
	previousFrameBuffer.resize(FRAME_SIZE);
	std::fill(previousFrameBuffer.begin(), previousFrameBuffer.end(), 0);
	frameBuffer.resize(FRAME_SIZE);
	std::fill(frameBuffer.begin(), frameBuffer.end(), 0); 

	return true;
}

bool AcquireFrame(DXGI_OUTDUPL_FRAME_INFO& frameInfo, ComPtr<IDXGIResource>& desktopResource) {
	HRESULT hr;

	// 새 프레임 가져오기
	hr = desktopDuplication->AcquireNextFrame(16, &frameInfo, &desktopResource);
	switch (hr) {
	case DXGI_ERROR_ACCESS_LOST:
		loge("Access lost");
		return false;
	case DXGI_ERROR_WAIT_TIMEOUT:
		loge("Timeout");
		return false;
	case DXGI_ERROR_INVALID_CALL:
		loge("Invalid call");
		return false;
	case S_OK:
		break;
	default:
		loge("Failed to acquire next frame");
		return false;
	}

	return true;
}

bool MapFrameToCPU(ComPtr<IDXGIResource>& desktopResource, ComPtr<ID3D11Texture2D>& acquiredTexture) {
	HRESULT hr;

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
		loge("Failed to create staging texture");
		desktopDuplication->ReleaseFrame();
		return false;
	}

	// GPU -> CPU 복사
	d3dContext->CopyResource(cpuTexture.Get(), acquiredTexture.Get());

	// 맵핑하여 데이터 가져오기
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	hr = d3dContext->Map(cpuTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
	if (FAILED(hr)) {
		loge("Failed to map texture");
		desktopDuplication->ReleaseFrame();
		return false;
	}

	// 데이터를 frameBuffer로 복사
	unsigned char* srcData = static_cast<unsigned char*>(mappedResource.pData);
	int rowPitch = mappedResource.RowPitch;

	for (int y = 0; y < _frameHeight; ++y) {
		memcpy(&frameBuffer[y * _frameWidth * 4], &srcData[y * rowPitch], _frameWidth * 4);
	}

	d3dContext->Unmap(cpuTexture.Get(), 0);
	desktopDuplication->ReleaseFrame();

	return true;
}

void calculateDiffSIMD(const uint8_t* currentFrame, const uint8_t* previousFrame, uint8_t* diffBuffer, size_t frameSize) {
	size_t i = 0;

	for (; i + 16 <= frameSize; i += 16) { // 16바이트씩 처리
		__m128i curr = _mm_loadu_si128(reinterpret_cast<const __m128i*>(currentFrame + i));
		__m128i prev = _mm_loadu_si128(reinterpret_cast<const __m128i*>(previousFrame + i));
		__m128i diff = _mm_xor_si128(curr, prev);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(diffBuffer + i), diff);
	}

	// 남은 부분 처리 (16바이트 단위 미만)
	for (; i < frameSize; ++i) {
		diffBuffer[i] = currentFrame[i] ^ previousFrame[i];
	}
}

void compressFrame(const uint8_t* currentFrame, const uint8_t* previousFrame, std::vector<unsigned char>& compressedData) {
	std::vector<unsigned char> diffBuffer(_frameWidth * _frameHeight * 4, 0);

	// 변경된 부분 계산
	calculateDiffSIMD(currentFrame, previousFrame, diffBuffer.data(), FRAME_SIZE);

	// LZ4 압축
	int maxCompressedSize = LZ4_compressBound(diffBuffer.size());
	compressedData.resize(maxCompressedSize);
	int compressedSize = LZ4_compress_fast(
		reinterpret_cast<const char*>(diffBuffer.data()), // unsigned char*를 const char*로 변환
		reinterpret_cast<char*>(compressedData.data()),
		diffBuffer.size(),
		maxCompressedSize,
		1);

	if (compressedSize > 0) {
		compressedData.resize(compressedSize); // 실제 크기로 축소
	}
	else {
		std::cerr << "Compression failed!" << std::endl;
	}
}

// 캡처 루프
void CaptureLoop(void (*frameCallback)(FrameData frameData)) {
	HRESULT hr;
	try {
		while (capturing) {
			ComPtr<IDXGIResource> desktopResource;
			DXGI_OUTDUPL_FRAME_INFO frameInfo;
			ComPtr<ID3D11Texture2D> acquiredTexture;

			auto startTime = std::chrono::high_resolution_clock::now();
			auto startEpochTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

			// 새 프레임 가져오기
			if (!AcquireFrame(frameInfo, desktopResource) && capturing) {
				continue;
			}

			// CPU로 프레임 데이터 복사 
			if (!MapFrameToCPU(desktopResource, acquiredTexture) && capturing) {
				continue;
			}

			// 프레임 압축
			std::vector<unsigned char> compressedData;
			compressFrame(frameBuffer.data(), previousFrameBuffer.data(), compressedData);

			// 콜백용 프레임 데이터 생성
			FrameData frameData;
			frameData.data = compressedData.data();
			frameData.width = _frameWidth;
			frameData.height = _frameHeight;
			frameData.frameRate = _targetFPS;

			frameData.dataSize = compressedData.size();
			frameData.timeStamp = startEpochTime;

			// 콜백 호출 (프레임 데이터 전달)
			if (!capturing)
				break;

			try {
				frameCallback(frameData);
			}
			catch (std::exception& e) {
				loge("Failed to call frame callback");
			}

			while (true) {
				auto now = std::chrono::high_resolution_clock::now();
				double elapsedTime = std::chrono::duration<double, std::milli>(now - startTime).count();
				if (elapsedTime >= frameTime) {
					break; // 목표 시간 도달 시 다음 프레임 진행
				}
			}
		}
	}
	catch (std::exception& e) {
		loge("Capture loop exception");
	}
}

// 캡처 시작
extern "C" __declspec(dllexport) void StartCapture(void (*frameCallback)(FrameData frameData), int frameWidth, int frameHeight, int frameRate) {
	_frameWidth = frameWidth;
	_frameHeight = frameHeight;
	FRAME_SIZE = _frameWidth * _frameHeight * 4;
	_targetFPS = frameRate;

	std::lock_guard<std::mutex> lock(captureMutex);
	if (!capturing) {
		if (!InitializeCapture()) {
			capturing = false;
			loge("Failed to initialize capture");
			return;
		}

		capturing = true;
		captureThread = std::thread(CaptureLoop, frameCallback);
	}
}

// 캡처 중지
extern "C" __declspec(dllexport) void StopCapture() {
	log("Stop capture");
	std::lock_guard<std::mutex> lock(captureMutex);
	capturing = false;
		
	if (captureThread.joinable()) {
		try {
			log("Joining captureThread");
			captureThread.join();
		}
		catch (...) {
			loge("Error while joining captureThread");
		}
	}

	// DirectX 자원 해제
	if (desktopDuplication) {
		log("Releasing desktopDuplication");
		desktopDuplication->Release();
		desktopDuplication = nullptr;
	}
	if (d3dContext) {
		log("Releasing d3dContext");
		d3dContext->Release();
		d3dContext = nullptr;
	}
	if (d3dDevice) {
		log("Releasing d3dDevice");
		d3dDevice->Release();
		d3dDevice = nullptr;
	}
	log("Capture stopped");
}