#pragma once
#include <string>
#include <chrono>
#include <format>

void loge(const std::string& errorMessage);
void log(const std::string& message);
void logd(const std::string& message, long long startTime);