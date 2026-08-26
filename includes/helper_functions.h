#pragma once
#include <string>

bool ParseHeaderLine(const std::string& line, std::string& outKey, std::string& outValue);
std::string Trim(const std::string& s);
bool ContainsCaseInsensitive(const std::string& forward, const std::string& searcher);