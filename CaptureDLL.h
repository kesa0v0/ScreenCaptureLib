// CaptureDLL.h
#pragma once
#define CAPTUREDLL_API __declspec(dllexport)
#include <vector> 

#include "ThreadPool.h"
#include "FrameData.h"

extern "C" {
    CAPTUREDLL_API const char* TestDLL();
    CAPTUREDLL_API void StartCapture(void (*frameCallback)(FrameData frameData), int frameWidth, int frameHeight, int frameRate);
    CAPTUREDLL_API void StopCapture();
}