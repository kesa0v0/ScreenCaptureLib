// CaptureDLL.h
#pragma once
#define CAPTUREDLL_API __declspec(dllexport)
#include <vector> 

struct FrameData {
    unsigned char* data;
    int width;
    int height;
    int frameRate;
    
    int dataSize;
    long long timeStamp;
};

extern "C" {
    CAPTUREDLL_API const char* TestDLL();
    CAPTUREDLL_API void StartCapture(void (*frameCallback)(FrameData frameData), int frameWidth, int frameHeight, int frameRate);
    CAPTUREDLL_API void StopCapture();
}