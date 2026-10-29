#include "debugger.h"
#include "core.h"
#include "coretimer.h"
#include "cpu.h"
#include "cpu_state.h"
#include "dma.h"
#include "gptimer.h"
#include "mmr.h"
#include "peripheral/cs42l52.h"
#include "sic.h"
#include "sport.h"
#include "twi.h"
#include <algorithm>
#include <stdexcept>
using Json = nlohmann::ordered_json;

template <size_t N> static std::vector<uint32_t> Words(const uint32_t (&v)[N]) { return {v, v + N}; }
CpuDebugger::CpuDebugger(BlackFinCpu &cpu) : cpu_(cpu), owner_(std::this_thread::get_id()) {
    if (!cpu.time_.Deterministic())
        throw std::invalid_argument("debugger requires deterministic execution");
    if (cpu.audioOutput_)
        throw std::invalid_argument("debugger does not support real-time host audio output");
    if (cpu.debugger_ || cpu.inRun_)
        throw std::logic_error("debugger attachment requires an unowned run boundary");
    const auto thread = cpu.cpuThread_.load();
    if (thread != std::thread::id{} && thread != owner_)
        throw std::logic_error("attach on CPU owner thread");
    originalChaining_ = cpu.chaining_;
    cpu.SetChaining(false);
    // set_chaining affects future translations only. Existing native loops
    // and chains must be discarded, including pending background promotions.
    cpu.InvalidateTranslations();
    cpu.debugger_ = this;
}
CpuDebugger::~CpuDebugger() {
    cpu_.debugger_ = nullptr;
    cpu_.SetChaining(originalChaining_);
    cpu_.InvalidateTranslations();
}
void CpuDebugger::Owner() const {
    if (std::this_thread::get_id() != owner_)
        throw std::logic_error("debugger state belongs to CPU owner thread");
}
void CpuDebugger::Inspectable() const {
    Owner();
    if (!stopped_)
        throw std::logic_error("inspection requires stopped CPU");
}
void CpuDebugger::RequestPause() {
    requested_.store(true, std::memory_order_release);
    cpu_.StopChaining();
}
bool CpuDebugger::Boundary(const char *phase) {
    Owner();
    if (stopped_)
        return true;
    const auto pc = cpu_.cpuState_->pc;
    if (suppress_ && cpu_.time_.Packets() != resumePackets_)
        suppress_ = false;
    const bool request = requested_.exchange(false, std::memory_order_acq_rel);
    auto target = targets_.find(pc);
    const bool hit = target != targets_.end() && !(suppress_ && pc == resumePc_);
    if (!request && !hit)
        return false;
    stopped_ = true;
    reason_ = request ? "pause" : "pc";
    phase_ = phase;
    if (hit) {
        target->second.verified = true;
        ++target->second.hits;
    }
    return true;
}
void CpuDebugger::Continue() {
    Inspectable();
    resumePc_ = cpu_.cpuState_->pc;
    resumePackets_ = cpu_.time_.Packets();
    suppress_ = true;
    requested_.store(false, std::memory_order_release);
    stopped_ = false;
    reason_ = "running";
}
void CpuDebugger::AddPcStop(uint32_t pc) {
    Owner();
    if (pc & 1)
        throw std::invalid_argument("PC target must be even");
    cpu_.emulator.InspectRam(pc, 2);
    if (targets_.size() >= 64 && !targets_.count(pc))
        throw std::invalid_argument("at most 64 PC stops");
    targets_.try_emplace(pc);
}
void CpuDebugger::RemovePcStop(uint32_t pc) {
    Owner();
    if (!targets_.erase(pc))
        throw std::invalid_argument("unknown PC stop");
}
std::vector<uint8_t> CpuDebugger::ReadRam(uint32_t address, uint32_t bytes) const {
    Inspectable();
    return cpu_.emulator.InspectRam(address, bytes);
}
Json CpuDebugger::Status() const {
    Owner();
    Json targets = Json::array();
    for (const auto &[pc, t] : targets_)
        targets.push_back({{"pc", pc},
                           {"verified", t.verified},
                           {"hits", t.hits},
                           {"state", t.hits ? "reached" : "unreached; armed only at exact dispatch/IDLE boundaries"}});
    return {{"stopped", stopped_},
            {"reason", reason_},
            {"phase", phase_},
            {"pc", cpu_.cpuState_->pc},
            {"cycles", cpu_.time_.Cycles()},
            {"ns", cpu_.time_.Nanoseconds()},
            {"packets", cpu_.time_.Packets()},
            {"chaining", false},
            {"idle_wait_pending", cpu_.debugIdlePending_},
            {"pc_precision", "exact run entry or post-service IDLE continuation; no packet splitting"},
            {"targets", targets}};
}
Json CpuDebugger::Snapshot() const {
    Inspectable();
    const auto &s = *cpu_.cpuState_;
    Json j = Status();
    j["cpu"] = {{"r", std::vector<uint32_t>(s.dpregs, s.dpregs + 8)},
                {"p", std::vector<uint32_t>(s.dpregs + 8, s.dpregs + 14)},
                {"sp", s.dpregs[14]},
                {"fp", s.dpregs[15]},
                {"usp", s.usp},
                {"ksp", s.ksp},
                {"rets", s.rets},
                {"reti", s.reti},
                {"retx", s.retx},
                {"retn", s.retn},
                {"rete", s.rete},
                {"astat", bcore::astat_compose(&s)},
                {"ax", Words(s.ax)},
                {"aw", Words(s.aw)},
                {"i", Words(s.iregs)},
                {"m", Words(s.mregs)},
                {"b", Words(s.bregs)},
                {"l", Words(s.lregs)},
                {"lt", Words(s.lt)},
                {"lc", Words(s.lc)},
                {"lb", Words(s.lb)},
                {"seqstat", s.seqstat},
                {"syscfg", s.syscfg},
                {"emudat", Words(s.emudat)},
                {"cycles_latched", Words(s.cycles)},
                {"idle", s.idle},
                {"halted", s.halted},
                {"exit_code", s.exit_code}};
    auto cec = [&](unsigned i) { return bcore::cec_mmr_read(cpu_.cpuState_.get(), bcore::CEC_MMR_BASE + 4 * i); };
    const auto &sic = *cpu_.sic;
    j["interrupts"] = {
        {"imask", cec(bcore::CEC_IDX_IMASK)}, {"ipend", cec(bcore::CEC_IDX_IPEND)}, {"ilat", cec(bcore::CEC_IDX_ILAT)},
        {"iprio", cec(bcore::CEC_IDX_IPRIO)}, {"sic_mask", Words(sic.imask)},       {"sic_pending", Words(sic.isr)},
        {"sic_wake", Words(sic.iwr)},         {"sic_routes", Words(sic.iar)},       {"sic_lines", sic.lines_}};
    const auto &timer = *cpu_.coreTimer;
    auto expiry = timer.NextExpiry();
    j["timer"] = {{"power", timer.power},
                  {"enabled", timer.enabled},
                  {"autoreload", timer.autoReload},
                  {"interrupt", timer.interrupt},
                  {"count", timer.CountAt(cpu_.time_.Cycles())},
                  {"period", timer.tperiod},
                  {"scale", timer.tscale},
                  {"expiry_cycles", expiry ? Json(*expiry) : Json(nullptr)}};
    Json events = Json::array();
    std::vector<std::pair<uint64_t, uint64_t>> sorted;
    for (const auto &e : cpu_.events_)
        sorted.emplace_back(e.dueNs, e.sequence);
    std::sort(sorted.begin(), sorted.end());
    for (size_t i = 0; i < std::min<size_t>(64, sorted.size()); ++i)
        events.push_back({{"ns", sorted[i].first}, {"sequence", sorted[i].second}});
    j["events"] = {{"count", sorted.size()},
                   {"next_ns", cpu_.nextEventNs_},
                   {"deadlines", events},
                   {"truncated", sorted.size() > 64}};
    const auto dma = cpu_.serviced_.dma->InspectDebugTx(4);
    const auto &d = dma.progress;
    j["dma4"] = {{"modeled", d.modeled},
                 {"running", d.running},
                 {"done", d.done},
                 {"sync", d.sync},
                 {"address", d.address},
                 {"remaining", d.remaining},
                 {"buffered", d.buffered},
                 {"work", d.work},
                 {"read", d.read},
                 {"accepted", d.accepted},
                 {"start_address", dma.startAddress},
                 {"configured_count", dma.configuredCount},
                 {"irq_status", dma.irqStatus},
                 {"fifo", dma.fifo}};
    Json sports = Json::array();
    for (const auto &sport : {cpu_.sport0, cpu_.sport1}) {
        const auto &t = sport->TxTransport();
        const auto p = t.Peek();
        auto in = t.InspectExternalInputs();
        sports.push_back({{"accepted", p.accepted},
                          {"transmitted", p.transmitted},
                          {"underflows", p.underflows},
                          {"overflows", p.overflows},
                          {"status", p.status},
                          {"supported", p.supported},
                          {"fifo", t.fifo_},
                          {"holding", t.hold_ ? Json(*t.hold_) : Json(nullptr)},
                          {"shifting", t.shift_ ? Json(*t.shift_) : Json(nullptr)},
                          {"shift_remaining", t.remaining_},
                          {"slot", t.slot_},
                          {"shift_slot", t.shiftSlot_},
                          {"shift_repeated", t.shiftRepeated_},
                          {"tcr1", t.tcr1_},
                          {"tcr2", t.tcr2_},
                          {"deadline_ns", t.NextEventNs()},
                          {"external_bit_hz", in.bitHz},
                          {"external_frame_clocks", in.frameClocks},
                          {"clock", in.clock},
                          {"sync", in.sync}});
    }
    j["sport"] = sports;
    if (cpu_.testCodec_) {
        const auto c = cpu_.testCodec_->Inspect();
        j["codec"] = {{"reset", c.reset},
                      {"mclk_hz", c.mclkHz},
                      {"bit_hz", c.bitHz},
                      {"frame_hz", c.frameHz},
                      {"clock", c.clock},
                      {"interface1", c.interface1},
                      {"interface2", c.interface2},
                      {"power1", c.power1},
                      {"power2", c.power2},
                      {"power3", c.power3},
                      {"master_mute", c.masterMute},
                      {"output_mute", c.outputMute},
                      {"control_available", c.controlAvailable},
                      {"initialized", c.initComplete},
                      {"master", c.master},
                      {"tristate", c.tristate},
                      {"clock_reason", c.clockReason}};
    } else
        j["codec"] = nullptr;
    j["timer_schedule"] = {{"gp_service_tick_cycles", cpu_.lastTimerCycles_ + cpu_.kCyclesPerTimerTick},
                           {"tmrclk_cycles_per_tick", cpu_.TmrclkCycles()},
                           {"tmrclk_ticks_to_change", cpu_.gptimer->TicksToChange(GPTimerClockTypeTMRCLK)},
                           {"panel_deadline_ns", cpu_.panel_.DeadlineNs()}};
    j["twi_deadline_ns"] = cpu_.serviced_.twi->NextByteNs();
    return j;
}
