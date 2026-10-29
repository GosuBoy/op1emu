// Synthetic GPIO bus regression: unsupported commands must not alias a
// previously selected register or invent an identification response.
#include "peripheral/oled.h"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>

static void Check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

class Panel : public OLED {
public:
    using OLED::OLED;
    unsigned writes = 0;
    void KnownRegister() {
        registers.at(0xf01).readCallback = [] { return 0x1234u; };
        registers.at(0xf01).writeCallback = [this](u32) { ++writes; };
    }
};

int main() {
    GPIO bus("data", 0x1000), ctl("control", 0x2000);
    std::array<GPIOPeripheral::GPIOConnection, 16> pins = {
        {{bus,0},{bus,1},{bus,2},{bus,3},{bus,4},{bus,5},{bus,6},{bus,7},
         {bus,8},{bus,9},{bus,10},{bus,11},{bus,12},{bus,13},{bus,14},{bus,15}}
    };
    Panel panel(pins, {ctl,0}, {ctl,1}, {ctl,2}, {ctl,3});
    panel.KnownRegister();
    std::vector<std::array<u32, 4>> events;
    panel.SetObserver([&](bool read, u32 command, bool supported, u32 value) {
        events.push_back({u32(read), command, u32(supported), value});
    });
    ctl.Write32(0, 15); ctl.Write32(0x30, 15);
    auto select = [&](u32 command) {
        ctl.Write32(8, 15); // deselect; RD/WR inactive
        bus.Write32(0x30, 65535);
        bus.Write32(0, command);
        ctl.Write32(4, 3); // CS and RS low
        ctl.Write32(4, 8); ctl.Write32(8, 8); // WR low/high
        ctl.Write32(8, 2); // RS high (data)
    };
    auto read = [&] {
        bus.Write32(0x30, 0); bus.Write32(0x40, 65535);
        ctl.Write32(4, 4); // RD low
        auto value = bus.Read32(0);
        ctl.Write32(8, 4);
        return value;
    };
    select(0xf01);
    Check(read() == 0x1234, "known register drives the input bus");
    select(0xab);
    for (unsigned i = 0; i < 5; ++i) {
        Check(read() == 0xab, "unsupported read leaves the bus latch alone");
        Check(events.back()[0] == 1 && events.back()[1] == 0xab && events.back()[2] == 0,
              "unsupported response is explicitly reported");
    }
    bus.Write32(0x30, 65535); bus.Write32(0, 0x7777);
    ctl.Write32(4, 8); ctl.Write32(8, 8);
    Check(panel.writes == 0, "unknown command must not write the previous known register");
    select(0xf01);
    Check(read() == 0x1234 && events.back()[2] == 1, "known register works after reselect");
    select(0xa1);
    for (unsigned n = 0; n < 5; ++n) Check(read() == 0xa1, "baseline has no panel identification reply");
    panel.SetSyntheticDdb({0x12, 0x34, 0x56, 0x78, 0x9a});
    select(0xa1);
    for (auto byte : *panel.SyntheticDdb()) {
        bus.Write32(0x30, 0); bus.Write32(0x40, 65535);
        ctl.Write32(4, 4);
        Check(bus.Read32(0) == byte, "synthetic bytes use real GPIO reads in order");
        ctl.Write32(4, 4); // held-low RD must not consume another byte
        Check(bus.Read32(0) == byte, "held low does not advance the reply");
        ctl.Write32(8, 4);
    }
    const auto end = events.size();
    Check(read() == 0x9a && events.size() == end, "exhausted reply does not invent byte six");
    select(0xa1);
    Check(read() == 0x12, "new command restarts synthetic reply");
    ctl.Write32(4, 2); // RS low: command read cannot consume a data byte
    Check(read() == 0x12, "command phase does not consume synthetic data");
    ctl.Write32(8, 2);
    Check(read() == 0x34, "data phase resumes at the next byte");
    ctl.Write32(8, 1); ctl.Write32(4, 1);
    const auto deselected = events.size();
    Check(read() == 0x34, "CS boundary prevents another synthetic byte");
    Check(events.size() == deselected + 1 && events.back()[2] == 0,
          "CS boundary clears synthetic command and returns to baseline path");
    select(0x1a1);
    Check(read() == 0x1a1, "profile does not mask an unrelated command to A1");
    select(0xf01);
    Check(read() == 0x1234, "synthetic profile preserves existing commands");
    std::puts("OLED supported/unsupported GPIO reads, no stale register alias, and reselect passed");
}
