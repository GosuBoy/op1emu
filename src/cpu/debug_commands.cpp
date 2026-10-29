#include "debug_commands.h"
#include <filesystem>
#include <stdexcept>
using Json = nlohmann::ordered_json;
DebugCommands::DebugCommands(CpuDebugger &debugger, const std::string &commands, const std::string &log)
    : debugger_(debugger), path_(commands) {
    if (!std::filesystem::is_regular_file(commands) ||
        std::filesystem::weakly_canonical(commands) == std::filesystem::weakly_canonical(log))
        throw std::invalid_argument("debugger requires distinct regular command file and writable log");
    log_.open(log);
    if (!log_)
        throw std::invalid_argument("cannot open debugger log");
    Emit({{"kind", "contract"},
          {"schema", 1},
          {"max_line_bytes", 4096},
          {"scheduling", "deterministic; chaining disabled"}});
}
void DebugCommands::Emit(const Json &j) {
    log_ << j.dump() << '\n';
    log_.flush();
    if (!log_)
        throw std::runtime_error("debug log write failed");
}
bool DebugCommands::Poll() {
    if (waiting_ && debugger_.Stopped()) {
        waiting_ = false;
        Emit({{"kind", "stop"}, {"status", debugger_.Status()}});
    }
    if (std::filesystem::file_size(path_) < offset_)
        throw std::runtime_error("command file truncated");
    std::ifstream input(path_, std::ios::binary);
    input.seekg(offset_);
    for (unsigned n = 0; n < 128; ++n) {
        std::string line;
        char c;
        while (line.size() <= 4096 && input.get(c) && c != '\n')
            line += c;
        if (line.size() > 4096)
            throw std::runtime_error("debug command exceeds 4096 bytes");
        if (!input || c != '\n')
            return false; // partial append: retry without consuming it
        offset_ += line.size() + 1;
        Json result = {{"kind", "response"}, {"sequence", sequence_++}};
        bool pause = false;
        try {
            const auto j = Json::parse(line);
            const auto cmd = j.at("cmd").get<std::string>();
            result["request"] = j;
            auto number = [&](const char *key) {
                const auto &v = j.at(key);
                uint64_t x;
                if (v.is_string()) {
                    auto s = v.get<std::string>();
                    size_t end = 0;
                    if (s.empty() || s[0] == '-')
                        throw std::invalid_argument("unsigned address required");
                    x = std::stoull(s, &end, 0);
                    if (end != s.size())
                        throw std::invalid_argument("invalid number");
                } else {
                    if (!v.is_number_unsigned() && !(v.is_number_integer() && v.get<int64_t>() >= 0))
                        throw std::invalid_argument("unsigned integer required");
                    x = v.get<uint64_t>();
                }
                if (x > UINT32_MAX)
                    throw std::invalid_argument("number exceeds 32 bits");
                return uint32_t(x);
            };
            if (cmd == "pause") {
                debugger_.RequestPause();
                pause = true;
                result["requested"] = true;
            } else if (cmd == "continue") {
                debugger_.Continue();
                result["status"] = debugger_.Status();
            } else if (cmd == "break") {
                debugger_.AddPcStop(number("pc"));
                result["status"] = debugger_.Status();
            } else if (cmd == "remove") {
                debugger_.RemovePcStop(number("pc"));
                result["status"] = debugger_.Status();
            } else if (cmd == "status")
                result["status"] = debugger_.Status();
            else if (cmd == "registers")
                result["cpu"] = debugger_.Snapshot().at("cpu");
            else if (cmd == "audio" || cmd == "snapshot")
                result["snapshot"] = debugger_.Snapshot();
            else if (cmd == "ram")
                result["bytes"] = debugger_.ReadRam(number("address"), number("bytes"));
            else if (cmd == "wait-stop") {
                waiting_ = true;
                result["ok"] = true;
                Emit(result);
                return false;
            } else if (cmd == "quit") {
                result["status"] = debugger_.Status();
                result["ok"] = true;
                Emit(result);
                Finish("quit");
                return true;
            } else
                throw std::invalid_argument("unknown debugger command");
            result["ok"] = true;
        } catch (const std::exception &e) {
            result["ok"] = false;
            result["error"] = e.what();
        }
        Emit(result);
        if (pause)
            return false; // let Run reach the pause boundary before later inspections
    }
    return false;
}
void DebugCommands::Finish(const std::string &reason) {
    Emit({{"kind", "finish"}, {"reason", reason}, {"status", debugger_.Status()}});
}
