// CaptureDLL.h
#pragma once
#define CAPTUREDLL_API __declspec(dllexport)

struct FrameData {
    unsigned char* data;
    int width;
    int height;
};

extern "C" {
    CAPTUREDLL_API const char* TestDLL();
    CAPTUREDLL_API void StartCapture(void (*frameCallback)(FrameData frameData));
    CAPTUREDLL_API void StopCapture();
}