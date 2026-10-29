#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>

class BlackFinCpu;
// One CPU owner thread; only RequestPause may be called on another thread.
// Read-only inspection is legal only while stopped. No saved machine copy.
class CpuDebugger {
  public:
    explicit CpuDebugger(BlackFinCpu &cpu);
    ~CpuDebugger();
    CpuDebugger(const CpuDebugger &) = delete;
    CpuDebugger &operator=(const CpuDebugger &) = delete;
    void RequestPause();
    void Continue();
    void AddPcStop(uint32_t pc);
    void RemovePcStop(uint32_t pc);
    bool Stopped() const {
        Owner();
        return stopped_;
    }
    nlohmann::ordered_json Status() const;
    nlohmann::ordered_json Snapshot() const;
    std::vector<uint8_t> ReadRam(uint32_t address, uint32_t bytes) const;

  private:
    friend class BlackFinCpu;
    bool Boundary(const char *phase);
    void Owner() const;
    void Inspectable() const;
    BlackFinCpu &cpu_;
    const std::thread::id owner_;
    std::atomic<bool> requested_{false};
    bool stopped_ = false, originalChaining_ = false, suppress_ = false;
    uint32_t resumePc_ = 0;
    uint64_t resumePackets_ = 0;
    const char *reason_ = "running";
    const char *phase_ = "run-entry";
    struct Target {
        bool verified = false;
        uint64_t hits = 0;
    };
    std::map<uint32_t, Target> targets_;
};
