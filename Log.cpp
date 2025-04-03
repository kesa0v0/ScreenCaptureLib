#include "Log.h"

#include <iostream>
#include <chrono>
#include <format>

void loge(const std::string& errorMessage) {
	// ���� �ð��� ��������
	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::current_zone()->to_local(now); // ���� �ð����� ��ȯ
	// �ð� ���� ���� (��:��:��)
	std::string timeStr = std::format("{:%H:%M:%S}", time);
	// �α� �޽��� ����   
	std::string logMessage = std::format("[{}] ERROR: {}", timeStr, errorMessage);
	// �ܼ� ���
	std::cerr << logMessage << std::endl;
}

void log(const std::string& message) {
	// ���� �ð��� ��������
	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::current_zone()->to_local(now); // ���� �ð����� ��ȯ
	// �ð� ���� ���� (��:��:��)
	std::string timeStr = std::format("{:%H:%M:%S}", time);
	// �α� �޽��� ����   
	std::string logMessage = std::format("[{}] INFO: {}", timeStr, message);
	// �ܼ� ���
	std::cout << logMessage << std::endl;
}

void logd(const std::string& message, long long startTime) {
	// ���� �ð��� ��������
	auto currTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	
	// ��� �ð� ���	
	long long elapsedTime = currTime - startTime;
	// �α� �޽��� ����
	std::string logMessage = std::format("[{}] DEBUG: {} ({}ms)", currTime, message, elapsedTime);
	// �ܼ� ���
	std::cout << logMessage << std::endl;
}