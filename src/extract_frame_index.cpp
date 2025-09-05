// Lightweight standalone implementation of extract_frame_index used by tests.
// Mirrors the logic found in recording_loader.cpp but avoids linking the full
// recording_loader (and its heavy Metavision dependencies / threads) into the
// unit test binary.

#include <string>
#include <cctype>

long long extract_frame_index(const std::string &pathStr) {
    auto filenamePos = pathStr.find_last_of("/\\");
    std::string name = (filenamePos==std::string::npos)? pathStr : pathStr.substr(filenamePos+1);
    auto dotPos = name.find_last_of('.');
    if (dotPos != std::string::npos) name = name.substr(0, dotPos);
    int i = static_cast<int>(name.size()) - 1;
    while (i >= 0 && !std::isdigit(static_cast<unsigned char>(name[i]))) --i;
    if (i < 0) return -1;
    int end = i;
    while (i >= 0 && std::isdigit(static_cast<unsigned char>(name[i]))) --i;
    std::string num = name.substr(i + 1, end - i);
    try { return std::stoll(num); } catch (...) { return -1; }
}
