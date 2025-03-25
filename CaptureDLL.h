// CaptureDLL.h
#pragma once
#define CAPTUREDLL_API __declspec(dllexport)
#include <vector> 

struct FrameData {
    std::vector<unsigned char> data;
	int size = 0;
    int width = 0;
    int height = 0;
    long long timeStamp = 0.0;
};

extern "C" {
    CAPTUREDLL_API const char* TestDLL();
    CAPTUREDLL_API void StartCapture(void (*frameCallback)(FrameData frameData));
    CAPTUREDLL_API void StopCapture();
}