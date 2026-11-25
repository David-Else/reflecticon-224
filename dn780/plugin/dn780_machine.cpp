#define DN780_IMPLEMENTATION
#include "dn780_machine.hpp"
#include "../core/engine.hpp"
#include "dn780_panel.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>

namespace dn780plug {
namespace {

// Our own inferred IC44 timing-PROM image (core/ic44_hypothesis.bin; the dn780 project's
// notes/prom-inference-*). Not a dump: no Klark Teknik bytes.
constexpr uint8_t kProm[32] = {0xcc, 0xcf, 0x0d, 0xcc, 0xcd, 0xcd, 0x2d, 0xed, 0xcd, 0x09, 0xc9,
                               0xcd, 0x84, 0xcd, 0xcd, 0x84, 0x8d, 0x8f, 0x8d, 0x8d, 0x8d, 0x9d,
                               0x8d, 0x8d, 0xc0, 0xcd, 0xcd, 0xc0, 0xcd, 0xcd, 0xcd, 0xcd};

// v2.2 programs offered (E039). 14h Sound-on-Sound and 15h Infinite are left out.
const char* const kPrograms[] = {"Hall", "Plate", "Chamber", "Room", "Alive",
                                 "Non-Linear", "Reverse", "Delay", "ADT", "Multitap Echo"};
constexpr int kNumPrograms = 10;
constexpr int kFirstProgram = 0x0A;

enum P { Program, Predelay, Pattern, Level, LF, HF, Decay, Size, Calibrated, InputLevel, NumParams };

// v2.2 ROM layout (notes/2026-11-10-0244-v22-param-map.md)
constexpr size_t kClampTable = 0x1CA0;   // 12 x 14 bytes for E039 0A-15: 7 maxima, 7 minima (E031-E037)
constexpr size_t kHexDigits = 0x38F3;    // 16 segment bytes for 0-F

bool is_reverb(int prog) { return prog >= 0x0A && prog <= 0x0D; }

class Dn780Machine final : public kit::Machine {
public:
    bool boot(const std::vector<uint8_t>& rom, const std::string& state, std::string* err) {
        if (rom.size() != 0x8000) {
            *err = "this is the v1.5 firmware: the plugin runs v2.2 for now";
            return false;
        }
        if (engine_.boot(rom.data(), rom.size(), kProm)) {
            *err = "the firmware did not start (not a DN780 v2.2 EPROM?)";
            return false;
        }
        if (!state.empty())   // the battery RAM's sequence and user memories from the session
            engine_.firmware().set_user_ram(reinterpret_cast<const uint8_t*>(state.data()), state.size());
        const auto& r = engine_.firmware().rom();
        for (int i = 0; i < 16; i++) seg_[r[kHexDigits + i]] = "0123456789AbCdEF"[i];
        // letters the firmware uses (the dn780 project's seg.py)
        const std::pair<uint8_t, char> letters[] = {{0x80, 'i'}, {0x42, 'r'}, {0x64, 'L'}, {0x46, 'c'}, {0x66, 't'},
                                                    {0xC6, 'o'}, {0xE5, 'G'}, {0xEA, 'H'}, {0x6B, 'P'}, {0xE2, 'h'},
                                                    {0xC2, 'n'}, {0x02, '-'}, {0x62, '+'}, {0x60, '1'}};
        for (auto& [b, c] : letters) seg_.emplace(b, c);
        seg_[0] = ' ';
        // the 8279's segment bits in the kit's order (a..g, dp): each bit matched to the segment
        // whose use across the firmware's own 0-F table it follows (dp = 0x10, as the readouts)
        static const uint8_t std7[16] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71};
        for (int b = 0; b < 8; b++) {
            if (b == 4) {
                bit_to_seg_[b] = 7;
                continue;
            }
            int best = -1, best_d = 99;
            for (int sgm = 0; sgm < 7; sgm++) {
                int d = 0;
                for (int i = 0; i < 16; i++) d += ((r[kHexDigits + i] >> b) & 1) != ((std7[i] >> sgm) & 1);
                if (d < best_d) best_d = d, best = sgm;
            }
            bit_to_seg_[b] = best;
        }
        return true;
    }

    void prepare(int host_rate, int max_block) override {
        engine_.prepare(host_rate, max_block);
        in_.assign(size_t(std::max(1, max_block)), 0.0f);
    }
    long latency() const override { return engine_.latency(); }

    void set_params(const std::vector<float>& v) override {
        if (v.size() > InputLevel)   // a linear pot: 5 = 0 dB
            gain_.store(std::max(0.0f, v[InputLevel]) / 5.0f, std::memory_order_relaxed);
        int pi = std::clamp(int(std::lround(v[Program])), 0, kNumPrograms - 1);
        int prog = kFirstProgram + pi;
        uint8_t bytes[7];
        const auto& r = engine_.firmware().rom();
        for (int k = 0; k < 7; k++) {
            int hi = r[kClampTable + 14 * (prog - 0x0A) + k], lo = r[kClampTable + 14 * (prog - 0x0A) + 7 + k];
            int x = int(std::lround(v[Predelay + k]));
            used_[k] = hi > lo;
            bytes[k] = uint8_t(std::clamp(x, lo, hi));
        }
        bool cal = v[Calibrated] > 0.5f;
        if (prog == prog_ && cal == cal_ && std::equal(bytes, bytes + 7, bytes_)) return;
        engine_.set_recorded_calibration(cal);
        // E030 = 0 (slot in use), E031-E037 the keys, E038 = 28h (a user memory: the display shows
        // the program's name), E039 the program, E03A = 0 (not edited)
        dn780::Firmware::Poke pokes[11] = {{0xE030, 0}, {0xE038, 0x28}, {0xE039, uint8_t(prog)}, {0xE03A, 0}};
        for (int k = 0; k < 7; k++) pokes[4 + k] = {uint16_t(0xE031 + k), bytes[k]};
        int rc = engine_.set_program(0, pokes, 11);
        if (rc != dn780::Firmware::Ok) {
            status_ = "the firmware refused this setting (" + std::to_string(rc) + ")";
            return;
        }
        prog_ = prog;
        cal_ = cal;
        std::copy(bytes, bytes + 7, bytes_);
        status_.clear();
        engine_.firmware().live_start();   // the free-running unit picks up from this setting
    }

    kit::Readouts readouts() const override {
        kit::Readouts o;
        o.param.assign(NumParams, "");
        if (prog_ < 0) {
            o.status = status_;
            return o;
        }
        const uint8_t* d = disp();
        auto digits = [&](std::initializer_list<int> ix) {
            std::string s;
            for (int i : ix) {
                auto it = seg_.find(uint8_t(d[i] & ~0x10));
                s += it == seg_.end() ? '?' : it->second;
                if (d[i] & 0x10) s += '.';
            }
            return s;
        };
        auto trim = [](std::string s) {   // leading blanks and zeros, keeping one digit before a point
            size_t k = 0;
            while (k + 1 < s.size() && (s[k] == ' ' || (s[k] == '0' && s[k + 1] != '.'))) k++;
            return s.substr(k);
        };
        bool rev = is_reverb(prog_);
        std::string pre = trim(digits({13, 14, 15}));
        std::string dec = trim(digits({3, 1}));
        std::string size = trim(digits({11, 8}));
        o.param[Program] = kPrograms[prog_ - kFirstProgram];
        o.param[Predelay] = pre + (pre.find('.') != std::string::npos ? " s" : " ms");
        {   // what the digit means depends on the program (the manual, pp. 19, 23-27)
            const std::string pat = trim(digits({12}));
            const char* what = prog_ == 0x12 ? " of 5 (voices)" : prog_ == 0x13 ? " of 9 (repeats)" : " of 9 (density)";
            o.param[Pattern] = pat.empty() ? pat : pat + what;
        }
        o.param[Level] = trim(digits({0}));
        o.param[LF] = trim(digits({2, 5}));
        o.param[HF] = trim(digits({6, 7}));
        if (rev) {   // the reverbs' meanings (the manual): level is depth, LF/HF are steps against the decay
            if (!o.param[Level].empty()) o.param[Level] += " of 9 (depth)";
            auto rel = [](std::string& s, const char* minus) {
                if (s.empty()) return;
                const int v = std::atoi(s.c_str());
                s += v == 0 ? " (as decay)" : v < 0 ? minus : " (longer)";
            };
            rel(o.param[LF], " (shorter)");
            rel(o.param[HF], " (darker)");
        }
        o.param[Decay] = rev ? dec + " s" : dec;
        o.param[Size] = size + " m";
        for (int k = 0; k < 7; k++)
            if (!used_[k]) o.param[Predelay + k] = "";
        o.param[Calibrated] = rev ? (cal_ ? "matched to recordings" : "design values") : "";
        {
            const float g = gain_.load(std::memory_order_relaxed);
            char b[32];
            if (g <= 0) std::snprintf(b, sizeof b, "off");
            else std::snprintf(b, sizeof b, "%+.1f dB", 20.0 * std::log10(g));
            o.param[InputLevel] = b;
        }
        // the panel left to right: name, predelay, pattern, level, decay, LF, HF, size
        o.panel = digits({9, 10}) + "  " + digits({13, 14, 15}) + "  " + digits({12}) + "  " + digits({0}) + "  " +
                  digits({3, 1}) + "  " + digits({2, 5}) + "  " + digits({6, 7}) + "  " + digits({11, 8});
        o.status = status_;   // only problems: the ROM line already names the firmware
        return o;
    }

    // ---- hardware panel (dn780_panel.hpp): the firmware itself, free running ----
    void panel_key(int id, bool down) override {
        auto& fw = engine_.firmware();
        if (!fw.live()) return;
        last_key_ = Clock::now();
        held_ = std::max(0, held_ + (down ? 1 : -1));
        if (id == KeyIn) {   // the analogue input mute, while held
            in_mute_.store(down, std::memory_order_relaxed);
            return;
        }
        const uint8_t bit = id == KeyDown ? 1 : id == KeyUp ? 2 : id == KeyRev ? 4 : 0;
        if (bit) {
            f001_ = down ? uint8_t(f001_ & ~bit) : uint8_t(f001_ | bit);
            fw.live_inputs(f001_);
        }
        if (id < 0x100 && down) fw.live_key(uint8_t(id));
        pump(dn780::Firmware::kClock / 50);   // at least 20 ms of the unit, so the key is taken now
    }

    kit::PanelState panel() const override {
        kit::PanelState o;
        if (prog_ < 0) return o;
        pump(0);
        o.valid = true;
        const uint8_t* d = disp();
        o.digits.resize(16);
        for (int i = 0; i < 16; i++) {
            uint8_t v = 0;
            if (i == 2 || i == 6) {   // the LF / HF sign digits: g = minus, f+e with it = plus
                v = uint8_t((d[i] & 0x02 ? 0x40 : 0) | (d[i] & 0x60 ? 0x02 : 0));
            } else if (i != 4) {      // digit 4 is the indicator LEDs
                for (int b = 0; b < 8; b++)
                    if ((d[i] >> b & 1) && bit_to_seg_[b] >= 0) v |= uint8_t(1u << bit_to_seg_[b]);
            }
            o.digits[size_t(i)] = v;
        }
        o.leds.assign(NumDnLeds, 0);
        for (int k = 0; k < NumParamLeds; k++) o.leds[size_t(LedParam0 + k)] = d[4] & kParamLedBit[k] ? 255 : 0;
        o.leds[LedStore] = d[4] & 0x80 ? 255 : 0;
        // a b c: the sign digits' unused segments (a, d of digit 2; a of digit 6); which is which is a guess
        o.leds[LedA] = d[2] & 0x01 ? 255 : 0;
        o.leds[LedB] = d[2] & 0x04 ? 255 : 0;
        o.leds[LedC] = d[6] & 0x01 ? 255 : 0;
        // the LM3915: the input after LEVEL, 3 dB steps from converter full scale, falling 20 dB/s
        const auto now = Clock::now();
        const float peak = meter_.exchange(0.0f, std::memory_order_relaxed);
        const float db = peak > 0 ? 20.0f * std::log10(peak) : -120.0f;
        const float dt = meter_at_ == Clock::time_point{} ? 0.0f : std::chrono::duration<float>(now - meter_at_).count();
        meter_at_ = now;
        meter_db_ = std::max(db, meter_db_ - 20.0f * dt);
        for (int k = 0; k < NumMeterLeds; k++) o.leds[size_t(LedMeter0 + k)] = meter_db_ >= -3.0f * float(k) ? 255 : 0;
        o.adopt = adopt_;
        o.adopt_serial = adopt_serial_;
        return o;
    }

    // The battery RAM's sequence and user memories (stored from the panel).
    std::string state() const override {
        auto b = engine_.firmware().user_ram();
        return std::string(b.begin(), b.end());
    }

    std::vector<kit::Preset> presets() const override {
        std::vector<kit::Preset> out;
        const auto& fw = engine_.firmware();
        int in_family[kNumPrograms] = {};
        for (int n = 1; n <= 39; n++) {
            const uint8_t* b = fw.preset_bytes(n);
            if (!b) continue;
            int prog = b[9];   // E039
            if (prog < kFirstProgram || prog >= kFirstProgram + kNumPrograms) continue;
            int pi = prog - kFirstProgram;
            kit::Preset p;
            char name[64];
            if (is_reverb(prog)) std::snprintf(name, sizeof name, "%02d %s %d", n, kPrograms[pi], ++in_family[pi]);
            else std::snprintf(name, sizeof name, "%02d %s", n, kPrograms[pi]);
            p.name = name;
            p.values.assign(Calibrated, 0.0f);   // the program and the seven keys; the calibration stays
            p.values[Program] = float(pi);
            for (int k = 0; k < 7; k++) p.values[Predelay + k] = float(b[1 + k]);
            out.push_back(std::move(p));
        }
        return out;
    }

    void process(const float* const* in, float* out_l, float* out_r, int n) override {
        const float g = in_mute_.load(std::memory_order_relaxed) ? 0.0f : gain_.load(std::memory_order_relaxed);
        float peak = 0;
        for (int done = 0; done < n;) {
            const int c = std::min(n - done, int(in_.size()));
            for (int i = 0; i < c; i++) {
                in_[size_t(i)] = in[0][done + i] * g;
                peak = std::max(peak, std::fabs(in_[size_t(i)]));
            }
            engine_.process(in_.data(), out_l + done, out_r + done, c);
            done += c;
        }
        float m = meter_.load(std::memory_order_relaxed);
        while (peak > m && !meter_.compare_exchange_weak(m, peak, std::memory_order_relaxed)) {}
    }

private:
    using Clock = std::chrono::steady_clock;
    const uint8_t* disp() const {
        const auto& fw = engine_.firmware();
        return fw.live() ? fw.live_display() : fw.display();
    }
    // Run the free-running unit for the time since the last call (while the panel is in use: a key
    // in the last minute, or one held), at least min_cycles; post what it loads and report the
    // settings it now holds as parameter values.
    void pump(long min_cycles) const {
        auto& fw = engine_.firmware();
        if (!fw.live()) return;
        const auto now = Clock::now();
        long cycles = 0;
        if (held_ > 0 || now - last_key_ < std::chrono::seconds(60)) {
            const double dt = last_pump_ == Clock::time_point{} ? 0.0 : std::chrono::duration<double>(now - last_pump_).count();
            cycles = long(std::min(dt, 0.1) * double(dn780::Firmware::kClock));
        }
        last_pump_ = now;
        cycles = std::max(cycles, min_cycles);
        while (cycles > 0) {   // 1 ms slices: a finished load is posted promptly
            const long c = std::min(cycles, dn780::Firmware::kClock / 1000);
            cycles -= c;
            if (fw.live_run(c)) take_live();
        }
    }
    void take_live() const {
        auto& fw = engine_.firmware();
        uint8_t uc[256], co[256];
        fw.live_program(uc, co);
        const int prog = fw.live_ram(0x39);
        if (engine_.post(uc, co, prog) == dn780::Engine::Unsupported) {
            status_ = "this program (S.O.S. / Infinite Room) is not supported: the sound stays on the last one";
            return;
        }
        status_.clear();
        if (prog < kFirstProgram || prog >= kFirstProgram + kNumPrograms) return;
        uint8_t bytes[7];
        for (int k = 0; k < 7; k++) bytes[k] = fw.live_ram(uint16_t(0x31 + k));
        if (prog == prog_ && std::equal(bytes, bytes + 7, bytes_)) return;
        const auto& r = fw.rom();
        for (int k = 0; k < 7; k++)
            used_[k] = r[kClampTable + 14 * (prog - 0x0A) + k] > r[kClampTable + 14 * (prog - 0x0A) + 7 + k];
        prog_ = prog;
        std::copy(bytes, bytes + 7, bytes_);
        adopt_.assign(NumParams, NAN);
        adopt_[Program] = float(prog - kFirstProgram);
        for (int k = 0; k < 7; k++) adopt_[size_t(Predelay + k)] = float(bytes[k]);
        adopt_serial_++;
    }

    mutable dn780::Engine engine_;   // the free-running unit runs from panel() (control thread)
    std::map<uint8_t, char> seg_;
    int bit_to_seg_[8] = {-1, -1, -1, -1, 7, -1, -1, -1};   // 8279 bit -> kit segment (a=0 .. g=6, dp=7)
    mutable int prog_ = -1;
    bool cal_ = false;
    mutable bool used_[7] = {};
    mutable uint8_t bytes_[7] = {};
    mutable std::string status_;
    // the panel (control thread)
    uint8_t f001_ = 0xFF;
    int held_ = 0;
    Clock::time_point last_key_;
    mutable Clock::time_point last_pump_, meter_at_;
    mutable float meter_db_ = -120.0f;
    mutable std::vector<float> adopt_;
    mutable uint32_t adopt_serial_ = 0;
    // shared with the audio thread
    std::atomic<float> gain_{1.0f};
    std::atomic<bool> in_mute_{false};
    mutable std::atomic<float> meter_{0.0f};
    std::vector<float> in_;   // the input after LEVEL / IN (audio thread)
};

std::unique_ptr<kit::Machine> boot(const kit::RomSet& roms, const std::string& state, std::string* err) {
    if (!roms.complete()) {
        *err = "no firmware";
        return nullptr;
    }
    auto m = std::make_unique<Dn780Machine>();
    if (!m->boot(roms.bytes(0), state, err)) return nullptr;
    return m;
}

kit::Info make_info() {
    kit::Info i;
    i.name = "Reflecticon DN780";
    i.state_tag = "DN780KIT";
    i.settings_name = "ReflecticonDN780";
    i.rom_env = "DN780_ROMPATH";
    i.inputs = 1;                 // the unit sums its inputs to mono
    i.tail_seconds = 20;
    using K = kit::Param;
    std::vector<std::string> progs(kPrograms, kPrograms + kNumPrograms);
    i.params = {
        {"program", "Program", K::Choice, 0, float(kNumPrograms - 1), 0, progs, ""},
        {"predelay", "Predelay", K::Int, 0, 255, 22, {}, ""},
        {"pattern", "Pattern", K::Int, 0, 9, 9, {}, ""},
        {"level", "Level", K::Int, 0, 9, 5, {}, ""},
        {"lf", "LF", K::Int, 0, 14, 7, {}, ""},
        {"hf", "HF", K::Int, 0, 14, 7, {}, ""},
        {"decay", "Decay", K::Int, 0, 26, 11, {}, ""},
        {"size", "Room size", K::Int, 0, 11, 1, {}, ""},
        {"calibrated", "Real-unit tail", K::Bool, 0, 1, 0, {}, ""},
        {"inputLevel", "Input", K::Float, 0, 10, 5, {}, ""},
    };
    // the boxes follow the panel's own brackets (REFLECTIONS, REVERBERATION)
    kit::group(i.params, "Program", {{"program", "Program"}, {"predelay", "Predelay"}});
    kit::group(i.params, "Reflections", {{"pattern", "Pattern"}, {"level", "Level"}});
    kit::group(i.params, "Reverberation", {{"decay", "Decay"}, {"lf", "LF"}, {"hf", "HF"}, {"size", "Room size"},
                                           {"calibrated", "Tail"}});
    kit::group(i.params, "In / Out", {{"inputLevel", "Input"}});
    i.groups = {"Program", "Reflections", "Reverberation", "In / Out"};
    i.mix_group = "In / Out";
    kit::describe(i.params, {
        {"program",
         "The program, run by the unit's own firmware. Greyed-out rows are keys this program does not use."},
        {"predelay",
         "Delay before the reverb starts, on the unit's own scale."},
        {"pattern",
         "The density of the early reflections, 0 (sparse, grainy) to 9 (dense), as the unit's manual describes it. On ADT it picks the number and spacing of the voices (1-5); on Multitap Echo, of the repeats (1-9)."},
        {"level",
         "The early reflections' level, which the manual calls depth: 0 sounds distant, 9 close."},
        {"lf",
         "Low-frequency decay, on the unit's own scale."},
        {"hf",
         "High-frequency decay, on the unit's own scale: lower values darken the tail."},
        {"decay",
         "Reverb time, on the unit's own scale (the readout shows it as the unit displays it)."},
        {"size",
         "Room size (the readout shows metres)."},
        {"calibrated",
         "Shapes the reverb tail to match recordings of a real unit instead of the design values."},
        {"inputLevel",
         "The front panel's LEVEL knob: 5 (straight up) is 0 dB, 10 is +6 dB, 0 is off."},
    });
    i.roms = {{"firmware EPROM",
               {0x4000, 0x8000},
               {{"cf13a9920cedce83c700a5144e954c8c61dcaa934426ab6306fdf7db3c0eb915", "v2.20"},
                {"aca82a65cc6faccd53ebfb5c745b18a6687f326ec705d5e495bf6bb515658582", "v1.5"}},
               {"dn780", "780"}}};
    i.panel = dn780_layout();
    i.about = "An emulation of a 1980s digital reverb from its firmware and schematics. "
              "Bring your own v2.2 EPROM image. Not affiliated with or endorsed by the maker.";
    return i;
}

}  // namespace

const kit::MachineType& machine_type() {
    static const kit::MachineType t{make_info(), &boot};
    return t;
}

}  // namespace dn780plug
