// CaptureDLL.cpp
#include "pch.h"
#include "CaptureDLL.h"
#include <windows.graphics.capture.h>
#include <thread>
#include <vector>
#include <mutex>


// ���� ����
bool capturing = false;
std::thread captureThread;
std::mutex captureMutex;

// ���� ĸó ������ (���� ���������� ȭ�� �����͸� ��������)
int frameWidth = 1920; // ���� �ػ�
int frameHeight = 1080; // ���� �ػ�
std::vector<unsigned char> frameBuffer(frameWidth* frameHeight * 4, 255); // ��� RGBA ������

// DLL�� �ε�Ǿ����� Ȯ���ϴ� �׽�Ʈ �Լ�
const char* TestDLL() {
    return "DLL is successfully loaded!";
}


void CaptureLoop(void (*frameCallback)(unsigned char* data, int width, int height)) {
    while (capturing) {
        // �����͸� ĸó�� �� Callback ȣ��
        frameCallback(frameBuffer.data(), frameWidth, frameHeight);
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // 30fps �ùķ��̼�
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