#pragma once
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

class BlackFinCpu;
class ObservedMemoryDevice;

// Optional bounded startup diagnostics. Never supplies a clock or device data.
// Contains guest data; callers choose a private destination for private images.
class StartupTrace {
public:
    // 0 preserves existing budgets; a nonzero cap limits every event source,
    // including the dedicated history. Contract/summary metadata is separate.
    StartupTrace(BlackFinCpu& cpu, std::string prefix, uint64_t sourceLimit = 0);
    ~StartupTrace();
    void BeforeRun();
    void Finish(const std::string& reason);
    void AddMemory(uint32_t address, uint32_t bytes);
    void AddCheckpoint(uint32_t pc);
    void Checkpoint(const char* label, bool memory = false);
private:
    void Emit(const std::string& json);
    bool Reserve(const char* category, uint64_t limit);
    void History(const std::string& json);
    BlackFinCpu& cpu_;
    std::string prefix_;
    uint64_t sourceLimit_ = 0;
    std::ofstream events_;
    std::ofstream hysteresis_;
    ObservedMemoryDevice* portMux_ = nullptr;
    uint64_t historyTotal_ = 0, historyWritten_ = 0, historyOmitted_ = 0;
    uint64_t historyReads_ = 0, historyWrites_ = 0, unattributedChanges_ = 0;
    uint32_t lastWord_ = 0;
    std::set<uint32_t> checkpoints_;
    uint64_t eventsWritten_ = 0, omitted_ = 0, nextProgress_ = 0;
    uint64_t accessCountersDropped_ = 0;
    std::map<uint32_t, uint64_t> pcs_;
    std::map<std::string, uint64_t> categoryEvents_, categoryOmissions_;
    std::map<std::pair<uint32_t, unsigned>, uint32_t> reads_;
    std::map<std::tuple<uint32_t, unsigned, bool>, uint64_t> accesses_;
    std::vector<std::pair<uint32_t, uint32_t>> memory_;
};
