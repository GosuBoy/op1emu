#pragma once
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

class BlackFinCpu;

// Optional bounded startup diagnostics. Never supplies a clock or device data.
// Contains guest data; callers choose a private destination for private images.
class StartupTrace {
public:
    StartupTrace(BlackFinCpu& cpu, std::string prefix);
    ~StartupTrace();
    void BeforeRun();
    void Finish(const std::string& reason);
    void AddMemory(uint32_t address, uint32_t bytes);
private:
    void Emit(const std::string& json);
    bool Reserve(const char* category, uint64_t limit);
    BlackFinCpu& cpu_;
    std::string prefix_;
    std::ofstream events_;
    uint64_t eventsWritten_ = 0, omitted_ = 0, nextProgress_ = 0;
    std::map<uint32_t, uint64_t> pcs_;
    std::map<std::string, uint64_t> categoryEvents_, categoryOmissions_;
    std::map<std::pair<uint32_t, unsigned>, uint32_t> reads_;
    std::map<std::tuple<uint32_t, unsigned, bool>, uint64_t> accesses_;
    std::vector<std::pair<uint32_t, uint32_t>> memory_;
};
