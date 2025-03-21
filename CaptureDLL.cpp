// CaptureDLL.cpp
#include "pch.h"
#include "CaptureDLL.h"
#include <windows.graphics.capture.h>
#include <thread>
#include <vector>
#include <mutex>


// 전역 변수
bool capturing = false;
std::thread captureThread;
std::mutex captureMutex;

// 예제 캡처 데이터 (실제 구현에서는 화면 데이터를 넣으세요)
int frameWidth = 1920; // 가로 해상도
int frameHeight = 1080; // 세로 해상도
std::vector<unsigned char> frameBuffer(frameWidth* frameHeight * 4, 255); // 흰색 RGBA 데이터

// DLL이 로드되었는지 확인하는 테스트 함수
const char* TestDLL() {
    return "DLL is successfully loaded!";
}


void CaptureLoop(void (*frameCallback)(unsigned char* data, int width, int height)) {
    while (capturing) {
        // 데이터를 캡처한 후 Callback 호출
        frameCallback(frameBuffer.data(), frameWidth, frameHeight);
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // 30fps 시뮬레이션
    }
}

void StartCapture(void (*frameCallback)(unsigned char* data, int width, int height)) {
    std::lock_guard<std::mutex> lock(captureMutex);
    if (!capturing) {
        capturing = true;
        captureThread = std::thread(CaptureLoop, frameCallback);
    }
}

void StopCapture() {
    {
        std::lock_guard<std::mutex> lock(captureMutex);
        capturing = false;
    }
    if (captureThread.joinable()) {
        captureThread.join();
    }
}