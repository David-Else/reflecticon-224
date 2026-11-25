#include "sg323_machine.hpp"
#include "sg323_panel.hpp"
#include "../core/sg323.hpp"
#include "../../shared/core/mailbox.hpp"
#include "../../shared/core/rate_bridge.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>

namespace sg323plug {
namespace {

// In the front panel's order (input, LF decay, HF decay, direct, reverb, room, pre delay, decay time, the three
// override switches), then the model's own options.
enum P { Input, LfDecay, HfDecay, DirectMix, ReverbMix, Room, Predelay, Decay, InputMute, DryOnly, ReverbClear,
         Modulation, Hiss, Unit, NumParams };

// The panel's input knob is marked 0-10. The pot's taper is unknown, so the gain is linear in the knob; 10 is
// 4x converter full scale for a full-scale input, 2.5 (the default) is the project's 0.5.
constexpr double kInputTop = 2.0;

// Decay time in seconds by room and knob position: the owner's manual's Table III, which the unit's
// decay display (the 0809 table) reproduces exactly.
//
// The hardware panel (sg323_panel.hpp) computes its displays and level meter here. On the unit the
// 0809 PROM looks like the display's 7-segment table (pages: room digit, decay units/tenths by
// room x decay, predelay digits, override bits, an 8-step peak thermometer; audit note
// 2026-11-18-0131); once display sheets 4 and 10 confirm its addressing it could drive them instead,
// as an optional ROM.
const double kDecaySec[8][8] = {{0, .4, .7, 1.0, 1.3, 1.6, 1.8, 2.0}, {0, .5, 1.0, 1.5, 2.0, 2.4, 2.7, 3.0},
                                {0, .8, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0}, {0, 1.0, 1.8, 2.5, 3.2, 3.8, 4.4, 5.0},
                                {0, 1.2, 2.2, 3.0, 3.8, 4.6, 5.4, 6.0}, {0, 1.5, 2.5, 3.5, 4.5, 5.4, 6.2, 7.0},
                                {0, 1.8, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0}, {0, 2.0, 3.5, 5.0, 6.5, 8.0, 9.0, 10.0}};

// The predelay knob in ms (the shortest audition tap - 1 ms, as the panel displays it), decoded from
// the PROM tables this project was built on: facts about the tables, no bytes.
const char* const kPredelay[16] = {"0 ms",  "10 ms", "20 ms",  "30 ms",  "40 ms",  "50 ms",  "60 ms",  "80 ms",
                                   "100 ms", "120 ms", "140 ms", "170 ms", "200 ms", "240 ms", "280 ms", "320 ms"};

std::string fmt(const char* f, double v) {
    char b[48];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

class Sg323Machine final : public kit::Machine {
public:
    bool boot(const kit::RomSet& roms, std::string* err) {
        auto take = [&](int i) { return roms.bytes(size_t(i)); };
        proms_.p0803 = take(0);
        proms_.p0804 = take(1);
        proms_.p0805 = take(2);
        proms_.p0806 = take(3);
        if (proms_.p0806.size() == 4096) proms_.p0806.resize(2048);   // a 4K read: identical halves
        proms_.p0807 = take(4);
        proms_.p0808 = take(5);
        if (!proms_.complete()) {
            *err = "PROM images of the wrong size";
            return false;
        }
        m_ = std::make_unique<sg323::Machine>();
        ctl_ = controls_for(defaults());
        if (!m_->init(proms_, ctl_)) {
            *err = "the PROMs did not load";
            return false;
        }
        m_->set(sg323::Coefs::make(ctl_), sg323::Taps::make(proms_, ctl_), ctl_);
        return true;
    }

    void prepare(int host_rate, int max_block) override {
        bridge_.setup(host_rate, max_block, long(sg323::FS_HI), 1, 0.0, 2);
    }
    long latency() const override { return bridge_.latency(); }
    bool live_readouts() const override { return true; }   // the input level LEDs

    void set_params(const std::vector<float>& v) override {
        v_ = v;
        sg323::Controls c = controls_for(v);
        if (c.hf_eq != ctl_.hf_eq || c.hf_shelf_db != ctl_.hf_shelf_db || c.deemph_trim != ctl_.deemph_trim ||
            c.lf_eq != ctl_.lf_eq || !coefs_sent_) {
            coefs_.post(sg323::Coefs::make(c));
            coefs_sent_ = true;
        }
        if (c.room != ctl_.room || c.decay != ctl_.decay || c.predelay != ctl_.predelay || c.modulation != ctl_.modulation ||
            c.dry_only != ctl_.dry_only || c.reverb_clear != ctl_.reverb_clear || !taps_sent_) {
            taps_.post(sg323::Taps::make(proms_, c));
            taps_sent_ = true;
        }
        levels_.post(c);
        ctl_ = c;
        for (auto it = pending_.begin(); it != pending_.end();)   // the host has taken a panel setting
            it = std::abs(v_[size_t(it->first)] - it->second) < 1e-4f ? pending_.erase(it) : std::next(it);
    }

    // ---- hardware panel (sg323_panel.hpp): the knobs are parameters; the keys set parameters ----
    void panel_key(int id, bool down) override {
        if (v_.size() < NumParams) return;
        const auto now = std::chrono::steady_clock::now();
        if (down && id == KeyInputMute) set_from_panel(InputMute, cur(InputMute) > 0.5f ? 0.0f : 1.0f);
        if (down && id == KeyDryOnly) set_from_panel(DryOnly, cur(DryOnly) > 0.5f ? 0.0f : 1.0f);
        if (id == KeyReverbClear) {   // clears while held, at least half a second
            if (down) {
                clear_down_ = now;
                clear_release_ = {};
                set_from_panel(ReverbClear, 1.0f);
            } else if (now - clear_down_ >= kClearHold) {
                set_from_panel(ReverbClear, 0.0f);
            } else {
                clear_release_ = clear_down_ + kClearHold;
            }
        }
    }

    kit::PanelState panel() const override {
        kit::PanelState s;
        if (v_.size() < NumParams || !m_) return s;
        const auto now = std::chrono::steady_clock::now();
        if (clear_release_ != Clock::time_point{} && now >= clear_release_) {
            clear_release_ = {};
            set_from_panel(ReverbClear, 0.0f);
        }
        s.valid = true;
        const sg323::Controls& c = ctl_;
        // the displays: room, pre delay in ms, decay in seconds (one decimal)
        int pdc[16];
        proms_.predelay_codes(pdc);
        const int ms = int(std::lround(proms_.predelay_ms(pdc[std::clamp(c.predelay, 0, 15)])));
        const double sec = c.decay == 0 ? 0.0 : kDecaySec[c.room - 1][c.decay];
        const int tenths = int(std::lround(sec * 10));
        s.digits.assign(7, 0);
        s.digits[0] = seg(c.room);
        s.digits[1] = ms >= 100 ? seg(ms / 100) : 0;
        s.digits[2] = ms >= 10 ? seg(ms / 10 % 10) : 0;
        s.digits[3] = seg(ms % 10);
        s.digits[4] = tenths >= 100 ? seg(tenths / 100) : 0;
        s.digits[5] = uint8_t(seg(tenths / 10 % 10) | 0x80);
        s.digits[6] = seg(tenths % 10);
        // the level meter: the input's peak after the input knob, falling 20 dB/s
        const float peak = meter_.exchange(0.0f, std::memory_order_relaxed);
        const float db = peak > 0 ? 20.0f * std::log10(peak / float(kFullScale)) : -120.0f;
        const float dt = meter_at_ == Clock::time_point{} ? 0.0f : std::chrono::duration<float>(now - meter_at_).count();
        meter_at_ = now;
        meter_db_ = std::max(db, meter_db_ - 20.0f * dt);
        s.leds.assign(NumSgLeds, 0);
        for (int i = 0; i < NumLevelLeds; i++) s.leds[size_t(LedLevel0 + i)] = meter_db_ >= kLevelDb[i] ? 255 : 0;
        s.leds[LedMute] = cur(InputMute) > 0.5f ? 255 : 0;   // what the keys last set, adopted or not
        s.leds[LedDry] = cur(DryOnly) > 0.5f ? 255 : 0;
        s.leds[LedClear] = cur(ReverbClear) > 0.5f ? 255 : 0;
        if (!pending_.empty()) {
            s.adopt.assign(NumParams, NAN);
            for (auto& [i, x] : pending_) s.adopt[size_t(i)] = x;
        }
        s.adopt_serial = adopt_serial_;
        return s;
    }

    kit::Readouts readouts() const override {
        kit::Readouts o;
        o.param.assign(NumParams, "");
        if (v_.size() < NumParams) return o;
        const sg323::Controls& c = ctl_;
        o.param[Room] = fmt("Room %.0f", c.room);
        o.param[Decay] = c.decay == 0 ? std::string("0: no reverb") : fmt("%.1f s", kDecaySec[c.room - 1][c.decay]);
        int pdc[16];
        proms_.predelay_codes(pdc);
        o.param[Predelay] = fmt("%.0f ms", proms_.predelay_ms(pdc[std::clamp(c.predelay, 0, 15)]));
        // LF / HF decay: RV2 / RV3 set the loop's (and, for HF, the input's) corners; max = least cut
        // the corners of the filters in the reverb loop: lower = the lows / highs die away sooner
        const double lf_hz = 1.0 / (2 * M_PI * 0.22e-6 * (1500.0 + c.lf_eq * 50000.0));
        const double hf_khz = 1.0 / (2 * M_PI * (3300.0 + c.hf_eq * 50000.0) * 1e-9) / 1000.0;
        o.param[LfDecay] = lf_hz < 20.0 ? std::string("full (no low cut)") : fmt("low cut %.0f Hz", lf_hz);
        o.param[HfDecay] = hf_khz > 20.0 ? std::string("full (no high cut)") : fmt("high cut %.1f kHz", hf_khz);
        o.param[Input] = fmt("%.1f", v_[Input]);
        o.param[InputMute] = c.input_mute ? "muted" : "off";
        // as the panel marks the knobs, 0-10
        o.param[ReverbMix] = fmt("%.1f", 10.0 * c.reverb_mix);
        o.param[DirectMix] = c.dry_only ? std::string("10 (dry only)") : fmt("%.1f", 10.0 * c.direct_mix);
        o.param[Modulation] = c.modulation ? "on" : "off";
        o.param[Hiss] = c.hiss_nv > 0 ? "on" : "off";
        o.param[DryOnly] = c.dry_only ? "dry signal only" : "off";
        o.param[ReverbClear] = c.reverb_clear ? "clearing" : "ready";
        o.param[Unit] = c.deemph_trim != 1.0 ? "de-emphasis 0.95, shelf -0.2 dB" : "design values";
        // the panel: the predelay display and the input level LEDs (the rate-level comparators,
        // which set how fast the modulation runs)
        int code = level_.load(std::memory_order_relaxed);
        std::string leds;
        for (int i = 0; i < 4; i++) leds += (code >> i) & 1 ? "#" : ".";
        o.panel = o.param[Room] + "   " + o.param[Predelay] + "   " + (c.decay == 0 ? std::string("0.0 s") : o.param[Decay]) +
                  "   IN " + leds + (c.input_mute ? "   INPUT MUTE" : "") + (c.dry_only ? "   DRY ONLY" : "") +
                  (c.reverb_clear ? "   REVERB CLEAR" : "");
        return o;
    }

    // The unit has no memories (rotary switches and knobs). These are ours, picked off Table III
    // (kDecaySec): they set the panel (the first 11 parameters, the overrides off), never the model
    // options. The blend is the unit's own direct and reverb knobs (there is no kit Mix here): direct
    // full, which at the default input (2.5) is the dry signal at about -6 dB (0.5 x G_DIR x V_FS/2 over
    // G_REV x WET_V = 0.49), and reverb = 0.49 x wet / (100 - wet) for the wet share named in the row.
    std::vector<kit::Preset> presets() const override {
        struct Row {
            const char* name;
            float lf, hf;
            int room, pre, decay;   // room 1-8, pre delay and decay as switch positions
            float mix;   // the wet share, %
        };
        static const Row rows[] = {
            {"Short ambience 0.4 s", 0.6f, 1.0f, 1, 0, 1, 30},
            {"Small room 0.7 s", 0.8f, 0.7f, 1, 1, 2, 25},
            {"Drum room 1.0 s", 0.8f, 0.6f, 2, 0, 2, 25},
            {"Slap and room 1.5 s", 0.9f, 0.7f, 2, 9, 3, 25},
            {"Vocal space 2.0 s", 0.9f, 0.8f, 3, 4, 3, 25},
            {"Medium hall 2.5 s", 1.0f, 0.7f, 4, 3, 3, 25},
            {"Dark chamber 3.0 s", 1.0f, 0.3f, 5, 2, 3, 25},
            {"Large hall 3.8 s", 0.9f, 0.6f, 5, 5, 4, 25},
            {"Big space 5.4 s", 1.0f, 0.5f, 6, 6, 5, 25},
            {"Cathedral 6.5 s", 1.0f, 0.5f, 8, 7, 4, 25},
            {"Endless 10 s", 1.0f, 0.4f, 8, 0, 7, 35},
        };
        std::vector<kit::Preset> out;
        for (const Row& r : rows) {
            kit::Preset p;
            p.name = r.name;
            const float rev = 0.49f * r.mix / (100.0f - r.mix);
            p.values = {2.5f, r.lf, r.hf, 1.0f, rev, float(r.room - 1), float(r.pre), float(r.decay), 0.0f, 0.0f, 0.0f};
            out.push_back(std::move(p));
        }
        return out;
    }

    void process(const float* const* in, float* out_l, float* out_r, int n) override {
        if (!bridge_.ready() || !m_) {
            std::fill(out_l, out_l + n, 0.0f);
            std::fill(out_r, out_r + n, 0.0f);
            return;
        }
        if (coefs_.take(coefs_tmp_)) m_->set_coefs(coefs_tmp_);
        if (taps_.take(taps_tmp_)) m_->set_taps(taps_tmp_);
        sg323::Controls lv;
        const bool have_lv = levels_.take(lv);
        if (have_lv) m_->set_levels(lv);
        if (have_lv) gain_ = float(lv.input_gain);
        float peak = 0;
        for (int i = 0; i < n; i++) peak = std::max({peak, std::fabs(in[0][i]), std::fabs(in[1][i])});
        sg323::Machine* m = m_.get();
        bridge_.process(in, out_l, out_r, n, [m](const float* const* x, float* l, float* r, int count) {
            for (int i = 0; i < count; i++) {
                double yl, yr;
                m->step(x[0][i], x[1][i], yl, yr);
                l[i] = float(yl);
                r[i] = float(yr);
            }
        });
        level_.store(m->level_code(), std::memory_order_relaxed);
        peak *= gain_;   // the meter's peak since the panel last read it
        float was = meter_.load(std::memory_order_relaxed);
        while (peak > was && !meter_.compare_exchange_weak(was, peak, std::memory_order_relaxed)) {}
    }

private:
    static std::vector<float> defaults();
    static sg323::Controls controls_for(const std::vector<float>& v) {
        sg323::Controls c;
        c.room = std::clamp(int(std::lround(v[Room])), 0, 7) + 1;
        c.decay = std::clamp(int(std::lround(v[Decay])), 0, 7);
        c.predelay = std::clamp(int(std::lround(v[Predelay])), 0, 15);
        c.lf_eq = std::clamp(double(v[LfDecay]), 0.0, 1.0);          // RV2: 1 = max LF decay (14 Hz)
        c.hf_eq = 1.0 - std::clamp(double(v[HfDecay]), 0.0, 1.0);    // RV3: max HF decay = widest (48 kHz)
        c.input_gain = kInputTop * std::clamp(double(v[Input]), 0.0, 10.0) / 10.0;
        c.input_mute = v[InputMute] > 0.5f;
        c.reverb_mix = v[ReverbMix];
        c.dry_only = v[DryOnly] > 0.5f;
        c.direct_mix = c.dry_only ? 1.0 : v[DirectMix];   // dry only: the taps off, the direct path full
        c.modulation = v[Modulation] > 0.5f;
        c.hiss_nv = v[Hiss] > 0.5f ? 212.0 : 0.0;
        c.reverb_clear = v[ReverbClear] > 0.5f;
        if (v[Unit] > 0.5f) {   // ravettel's unit (notes 2026-11-11-0500)
            c.deemph_trim = 0.95;
            c.hf_shelf_db = -0.2;
        }
        return c;
    }

    sg323::Proms proms_;
    std::unique_ptr<sg323::Machine> m_;
    kit::RateBridgeT<32> bridge_;
    kit::Mailbox<sg323::Coefs> coefs_;
    kit::Mailbox<sg323::Taps> taps_;
    kit::Mailbox<sg323::Controls> levels_;
    sg323::Coefs coefs_tmp_;   // audio thread
    sg323::Taps taps_tmp_;
    std::atomic<int> level_{0};
    sg323::Controls ctl_;      // control thread
    std::vector<float> v_;
    bool coefs_sent_ = false, taps_sent_ = false;

    // the panel (control thread; panel() is const but runs there too)
    using Clock = std::chrono::steady_clock;
    static constexpr auto kClearHold = std::chrono::milliseconds(500);
    static constexpr double kFullScale = 0.5;   // the input stage's value at converter full scale (kInputTop)
    static uint8_t seg(int d) {
        static const uint8_t k[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
        return k[std::clamp(d, 0, 9)];
    }
    // a setting made on the panel, offered to the host (PanelState::adopt) until it shows in v_
    void set_from_panel(int param, float value) const {
        pending_[param] = value;
        adopt_serial_++;
    }
    // a key's setting: what the panel last set if the host has not taken it yet
    float cur(int param) const {
        auto it = pending_.find(param);
        return it != pending_.end() ? it->second : v_[size_t(param)];
    }
    mutable std::map<int, float> pending_;
    mutable uint32_t adopt_serial_ = 0;
    Clock::time_point clear_down_;
    mutable Clock::time_point clear_release_;
    mutable Clock::time_point meter_at_;
    mutable float meter_db_ = -120.0f;
    mutable std::atomic<float> meter_{0.0f};   // audio thread: the input peak since the panel read it
    float gain_ = 0.5f;                // audio thread: the input knob's gain
};

std::unique_ptr<kit::Machine> boot(const kit::RomSet& roms, const std::string&, std::string* err) {
    if (!roms.complete()) {
        *err = "PROMs missing";
        return nullptr;
    }
    auto m = std::make_unique<Sg323Machine>();
    if (!m->boot(roms, err)) return nullptr;
    return m;
}

kit::Info make_info() {
    kit::Info i;
    i.name = "Reflecticon StarGate 323";
    i.state_tag = "SG323KIT";
    i.settings_name = "ReflecticonSG323";
    i.rom_env = "SG323_ROMPATH";
    i.inputs = 2;
    i.tail_seconds = 10;
    i.own_mix = true;   // direct and reverb are the panel's knobs: they are the wet/dry
    using K = kit::Param;
    std::vector<std::string> rooms, decays, pre;
    for (int r = 1; r <= 8; r++) rooms.push_back("Room " + std::to_string(r));
    for (int d = 0; d <= 7; d++) decays.push_back(d == 0 ? "0 (no reverb)" : std::to_string(d));
    for (auto* p : kPredelay) pre.push_back(p);
    // Labels as on the panel. "inputLevel" and "hfDecay" are new ids (2026-11-18): the old "input" was in dB
    // and the old "hfEq" ran the other way, so old sessions fall back to the defaults for those two.
    i.params = {
        {"inputLevel", "Input", K::Float, 0, 10, 2.5f, {}, ""},
        {"lfEq", "LF decay", K::Float, 0, 1, 1, {}, ""},
        {"hfDecay", "HF decay", K::Float, 0, 1, 1, {}, ""},
        {"directMix", "Direct mix", K::Float, 0, 1, 0, {}, ""},
        {"reverbMix", "Reverb mix", K::Float, 0, 1, 1, {}, ""},
        {"room", "Room", K::Choice, 0, 7, 2, rooms, ""},
        {"predelay", "Predelay", K::Choice, 0, 15, 0, pre, ""},
        {"decay", "Decay time", K::Choice, 0, 7, 5, decays, ""},
        {"inputMute", "Input mute", K::Bool, 0, 1, 0, {}, ""},
        {"dryOnly", "Dry only", K::Bool, 0, 1, 0, {}, ""},
        {"reverbClear", "Reverb clear", K::Bool, 0, 1, 0, {}, ""},
        {"modulation", "Modulation", K::Bool, 0, 1, 1, {}, ""},
        {"hiss", "Input hiss", K::Bool, 0, 1, 1, {}, ""},
        {"unit", "Unit", K::Choice, 0, 1, 0, {"design values", "ravettel's unit"}, ""},
    };
    kit::group(i.params, "Reverb", {{"room", "Room"}, {"predelay", "Predelay"}, {"decay", "Decay time"},
                                    {"lfEq", "LF decay"}, {"hfDecay", "HF decay"}});
    kit::group(i.params, "In / Out", {{"inputLevel", "Input"}, {"directMix", "Direct"}, {"reverbMix", "Reverb"}});
    kit::group(i.params, "Switches", {{"inputMute", "Input mute"}, {"dryOnly", "Dry only"}, {"reverbClear", "Reverb clear"}});
    kit::group(i.params, "Unit", {{"modulation", "Modulation"}, {"hiss", "Input hiss"}, {"unit", "Calibration"}});
    i.groups = {"Reverb", "In / Out", "Switches", "Unit"};
    kit::momentary(i.params, {{"reverbClear", "Clear"}});
    i.panel = sg323_layout();
    kit::describe(i.params, {
        {"inputLevel",
         "Input level into the reverb. As on the hardware, it also sets the modulation rate."},
        {"lfEq",
         "Low-frequency decay."},
        {"hfDecay",
         "High-frequency decay: lower values darken the tail."},
        {"directMix",
         "The dry signal's level in the unit's own output mix (the panel's direct knob, 0-10). With Reverb mix it is this plugin's wet/dry: 0 for the reverb alone, as on an aux send."},
        {"reverbMix",
         "The reverb's level in the unit's own output mix (the panel's reverb knob, 0-10)."},
        {"room",
         "The room program: the PROMs' delay patterns."},
        {"predelay",
         "Delay before the reverb starts."},
        {"decay",
         "Reverb time."},
        {"inputMute",
         "Mutes the reverb's input: the tail rings out."},
        {"dryOnly",
         "Outputs the dry signal only: the reverb taps off and the direct path at full level, whatever Direct mix is set to."},
        {"reverbClear",
         "Hold to clear the reverb memory, cutting the tail off (a click holds it half a second)."},
        {"modulation",
         "Slow delay modulation that smooths the tail's metallic ring."},
        {"hiss",
         "Adds the input stage's hiss, as on the hardware."},
        {"unit",
         "Values from the schematics, or the trims measured on one real unit."},
    });
    // U71 modulation rate, U76/U77 gain modulation, U78 gains, U79 delay modulation, U69 delays;
    // binary images (tools/prom_bin.py in the StarGate project turns text dumps into these)
    i.roms = {
        {"0803", {256, 128},
         {{"780033e07ede7be89bddadf796f90725b1c7bd0db69c9d64bc89f1ee03325c9a", "RevC"},
          {"88358599ace2c9516ad51d5f046ef9322c08c301d15e2d6186853cb2a117c79b", "RevC (128 bytes)"}},
         {"0803"}},
        {"0804", {4096}, {{"f75c0dabd623bbc47f17680286dd7a41e30b92068efc7c4f5509df955f1555d7", "RevA"}}, {"0804"}},
        {"0805", {4096}, {{"6c0cfd6def384b39f72689facc108913b8634aae466a0032334f156c683184f4", "RevA"}}, {"0805"}},
        {"0806", {2048, 4096},
         {{"1b331ee98ed399c2046343055669426af8b2b6eb6130875a8098f4c96407f74a", "RevC"},
          {"ece8fb92e287236322cada76f277fa0bc09d09d689225e3d07e111ded0fa9545", "RevC (4K read)"}},
         {"0806"}},
        {"0807", {4096}, {{"c859cee1e62a60f20fc7ba3d002258788d73328c98de25e2de71b5d890105d1a", "RevA"}}, {"0807"}},
        {"0808", {8192}, {{"9ae66543ef656ee1602da98b2f8f55c93886601153c453ce7b8fdab6e8c9b038", "RevC"}}, {"0808"}},
    };
    i.about = "An emulation of a 1980s digital reverb from its schematics and PROM tables. Bring your own "
              "PROM images (0803-0808, binary). Not affiliated with or endorsed by the maker.";
    return i;
}

std::vector<float> Sg323Machine::defaults() {
    std::vector<float> v;
    for (auto& p : make_info().params) v.push_back(p.def);
    return v;
}

}  // namespace

const kit::MachineType& machine_type() {
    static const kit::MachineType t{make_info(), &boot};
    return t;
}

}  // namespace sg323plug
