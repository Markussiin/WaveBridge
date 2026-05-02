#include "Cli.h"
#include "MockPhone.h"
#include "SelfTest.h"
#include "Sender.h"

#include <exception>
#include <iostream>

int main(int argc, char** argv)
{
    const auto parsed = wb::parseArgs(argc, argv);
    const std::string exeName = argc > 0 && argv[0] != nullptr ? argv[0] : "WaveBridge.exe";

    if (!parsed.ok) {
        std::cerr << parsed.error << "\n\n";
        wb::printUsage(std::cerr, exeName);
        return 2;
    }

    if (parsed.showHelp || parsed.config.mode == wb::AppMode::Help) {
        wb::printUsage(std::cout, exeName);
        return 0;
    }

    try {
        switch (parsed.config.mode) {
        case wb::AppMode::Send:
            return wb::runSender(parsed.config);
        case wb::AppMode::MockPhone:
            return wb::runMockPhone(parsed.config);
        case wb::AppMode::SelfTest:
            return wb::runSelfTests();
        case wb::AppMode::Help:
            wb::printUsage(std::cout, exeName);
            return 0;
        }
    } catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
