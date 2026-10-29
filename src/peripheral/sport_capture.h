#pragma once
#include "cpu/sport_tx.h"
#include <fstream>
#include <string>

// Native right-justified words, little endian (16- or 32-bit containers).
// No host audio device, conversion, resampling, padding, or clockless samples.
class SportCapture {
public:
    explicit SportCapture(const std::string& prefix) {
        pcm_.exceptions(std::ios::badbit | std::ios::failbit);
        trace_.exceptions(std::ios::badbit | std::ios::failbit);
        pcm_.open(prefix + ".pcm", std::ios::binary);
        trace_.open(prefix + ".csv");
        trace_ << "time_ns,index,value,bits,slot,lsb_first,repeated,valid\n";
    }
    void Write(const SportTxTransport::Word& word) {
        if (word.valid)
            for (unsigned byte = 0; byte < (word.bits > 16 ? 4u : 2u); ++byte)
                pcm_.put(char(word.value >> (byte * 8)));
        trace_ << word.ns << ',' << word.index << ',' << word.value << ',' << unsigned(word.bits)
               << ',' << unsigned(word.slot) << ',' << word.lsbFirst << ',' << word.repeated << ',' << word.valid << '\n';
    }
    void Finish() { pcm_.close(); trace_.close(); }
private:
    std::ofstream pcm_, trace_;
};
