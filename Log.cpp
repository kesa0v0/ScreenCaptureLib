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