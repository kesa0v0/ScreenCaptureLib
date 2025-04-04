#include "Log.h"

#include <iostream>
#include <chrono>
#include <format>

void loge(const std::string& errorMessage) {
	// 현재 시간을 가져오기
	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::current_zone()->to_local(now); // 지역 시간으로 변환
	// 시간 포맷 설정 (시:분:초)
	std::string timeStr = std::format("{:%H:%M:%S}", time);
	// 로그 메시지 생성   
	std::string logMessage = std::format("[{}] ERROR: {}", timeStr, errorMessage);
	// 콘솔 출력
	std::cerr << logMessage << std::endl;
}

void log(const std::string& message) {
	// 현재 시간을 가져오기
	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::current_zone()->to_local(now); // 지역 시간으로 변환
	// 시간 포맷 설정 (시:분:초)
	std::string timeStr = std::format("{:%H:%M:%S}", time);
	// 로그 메시지 생성   
	std::string logMessage = std::format("[{}] INFO: {}", timeStr, message);
	// 콘솔 출력
	std::cout << logMessage << std::endl;
}

void logd(const std::string& message, long long startTime) {
	// 현재 시간을 가져오기
	auto currTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	
	// 경과 시간 계산	
	long long elapsedTime = currTime - startTime;
	// 로그 메시지 생성
	std::string logMessage = std::format("[{}] DEBUG: {} ({}ms)", currTime, message, elapsedTime);
	// 콘솔 출력
	std::cout << logMessage << std::endl;
}