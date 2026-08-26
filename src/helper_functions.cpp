#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include "helper_functions.h"

// trim whitespaces
std::string Trim(const std::string& s) { 
	size_t start = s.find_first_not_of(" \t");
    size_t end = s.find_last_not_of(" \t");

	if (start == std::string::npos) {
        return "";
	}

	return s.substr(start, end - start + 1);
}

// Parse Key:Value pairs
bool ParseHeaderLine(const std::string& line, std::string& outKey, std::string& outValue) {
    size_t colonPos = line.find(':');
    if (colonPos == std::string::npos) {
        return false;
    }

    outKey = Trim(line.substr(0, colonPos));
    outValue = Trim(line.substr(colonPos + 1));

    return !outKey.empty();
}

// Do a case-insensitive search
bool ContainsCaseInsensitive(const std::string& forward, const std::string& searcher) {
    auto it = std::search(forward.begin(), forward.end(), searcher.begin(), searcher.end(),
        [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b); 
        });

    return it != forward.end();
}