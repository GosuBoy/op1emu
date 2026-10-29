#pragma once
#include "debugger.h"
#include <fstream>
#include <string>
// Bounded append-only JSONL regular-file mailbox, polled by the CPU owner.
// No command thread, network listener or blocking stdin read.
class DebugCommands {
  public:
    DebugCommands(CpuDebugger &debugger, const std::string &commands, const std::string &log);
    bool Poll(); // true: explicit quit
    void Finish(const std::string &reason);

  private:
    void Emit(const nlohmann::ordered_json &value);
    CpuDebugger &debugger_;
    std::string path_;
    std::ofstream log_;
    uint64_t offset_ = 0, sequence_ = 0;
    bool waiting_ = false;
};
