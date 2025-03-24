// CaptureDLL.h
#pragma once
#define CAPTUREDLL_API __declspec(dllexport)
#include <vector> 

struct FrameData {
    std::vector<unsigned char> data;
	int size;
    int width;
    int height;
    long long timeStamp;
};

extern "C" {
    CAPTUREDLL_API const char* TestDLL();
    CAPTUREDLL_API void StartCapture(void (*frameCallback)(FrameData frameData));
    CAPTUREDLL_API void StopCapture();
}