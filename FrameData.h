#pragma once

struct FrameData {
    unsigned char* data;
    int width;
    int height;
    int frameRate;

    int dataSize;
    long long timeStamp;
};