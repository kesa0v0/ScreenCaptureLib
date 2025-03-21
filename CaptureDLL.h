// CaptureDLL.h
#pragma once
#define CAPTUREDLL_API __declspec(dllexport)

extern "C" {
    CAPTUREDLL_API const char* TestDLL();
    CAPTUREDLL_API void StartCapture(void (*frameCallback)(unsigned char* data, int width, int height));
    CAPTUREDLL_API void StopCapture();
}