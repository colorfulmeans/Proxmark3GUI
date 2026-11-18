// Native, offline test fixture. Never talks to a real Proxmark3 or a network.
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#endif

static std::string hex(const std::string &input)
{
    const char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char byte : input) {
        result += digits[byte >> 4];
        result += digits[byte & 0x0f];
    }
    return result;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    // Keep the fixture protocol byte-identical on Windows. The CRT's default
    // text mode would turn each LF into CRLF, while the tests expect raw LF.
    if (_setmode(_fileno(stdout), _O_BINARY) == -1)
        return 91;
#endif

    if (argc != 3 || std::string(argv[1]) != "COM3" || std::string(argv[2]) != "-f")
        return 90;

    std::string mode;
    std::ifstream modeFile("fixture-mode.txt");
    std::getline(modeFile, mode);
    {
        std::ofstream started("fixture-started.txt", std::ios::app);
        started << "started\n";
    }
    if (mode == "exit-on-start")
        return 24;

    if (mode == "split-banner") {
        std::cout << '[' << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "=] Native offline fixture\n" << std::flush;
    } else {
        std::cout << "[=] Native offline fixture\n" << std::flush;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        std::cout << "RX:" << hex(line) << '\n' << std::flush;
        if (line == "hw version") {
            if (mode == "split-version") {
                std::cout << "OS...." << std::flush;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::cout << ".... fixture-native v1\n" << std::flush;
            } else {
                std::cout << "OS........ fixture-native v1\n" << std::flush;
            }
        }
        if (line == "rem fixture-exit")
            return 23;
    }
    return 0;
}
