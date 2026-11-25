// The StarGate 323 front panel for the kit's hardware mode (kit::PanelLayout), measured from a
// straight-on reference photo (the StarGate project's photos/stargate323_front_straight_sn014, with
// the lit-display photos for the digits; local reference only: nothing of them is shipped, this is a
// vector drawing at their positions). 19" 2U, millimetres (4.576 photo px per mm).
//
// The unit has no processor: its knobs and rotary switches are the plugin's parameters (the room,
// pre delay and decay switches are 8-, 16- and 8-position rotaries, drawn as the red knobs). The
// displays show what the switches select; the level LEDs are the input meter (computed by the
// plugin; 0 dB taken as the converter's full scale, an assumption); the override LEDs light with
// their keys.
//
// Ids:
//   keys     1 input mute, 2 dry only (both push on / push off), 3 reverb clear (held; a click holds
//            it half a second); the power key is drawn only (-1)
//   LEDs     0-7 level -36 ... 0 dB, 8-10 override: input mute, dry only, reverb clear
//   digits   0 room, 1-3 ms, 4-6 sec
#pragma once
#include "../../shared/core/panel.hpp"

namespace sg323plug {

enum SgKey { KeyInputMute = 1, KeyDryOnly = 2, KeyReverbClear = 3 };
enum SgLed { LedLevel0 = 0, NumLevelLeds = 8, LedMute = 8, LedDry = 9, LedClear = 10, NumSgLeds = 11 };
constexpr float kLevelDb[NumLevelLeds] = {-36, -30, -24, -18, -12, -6, -3, 0};

inline kit::PanelLayout sg323_layout() {
    using E = kit::PanelElement;
    kit::PanelLayout L;
    L.width = 483.0f;
    L.height = 88.9f;
    L.face = 0xff50524e;   // the grey-green anodised face
    L.ink = 0xffe8e8e2;
    L.leds = NumSgLeds;
    L.digits = 7;
    auto add = [&](E e) { L.elements.push_back(std::move(e)); };
    auto text = [&](float x, float y, const char* s, float size = 2.4f, int just = E::Centre) {
        E e;
        e.kind = E::Text;
        e.x = x, e.y = y, e.text = s, e.size = size, e.style = just;
        add(e);
    };
    auto hole = [&](float cx, float cy, int style) {
        E e;
        e.kind = E::Hole;
        float w = style == 0 ? 9.0f : 4.4f, h = style == 0 ? 6.4f : w;
        e.x = cx - w / 2, e.y = cy - h / 2, e.w = w, e.h = h, e.style = style;
        add(e);
    };
    auto window = [&](float x0, float y0, float x1, float y1, uint32_t tint, int style) {
        E e;
        e.kind = E::Window;
        e.x = x0, e.y = y0, e.w = x1 - x0, e.h = y1 - y0, e.colour = tint, e.style = style;
        add(e);
    };
    auto frame = [&](float x0, float y0, float x1, float y1, const char* caption) {
        E e;
        e.kind = E::Frame;
        e.x = x0, e.y = y0, e.w = x1 - x0, e.h = y1 - y0, e.text = caption, e.size = 2.3f, e.colour = 0xff7e807c;   // dim grey in the photos
        add(e);
    };
    auto led = [&](float cx, float cy, int id, float d = 1.8f) {
        E e;
        e.kind = E::Led;
        e.x = cx - d / 2, e.y = cy - d / 2, e.w = e.h = d, e.id = id, e.colour = 0xffff3424;
        add(e);
    };
    auto digit = [&](float cx, int id) {
        E d;
        d.kind = E::Digit;
        d.w = 5.4f, d.h = 8.2f, d.x = cx - d.w / 2, d.y = 25.8f - d.h / 2, d.id = id, d.colour = 0xffff2a1a;
        d.size = 2.1f;   // ~12 % of the height (the lit photos, ~15 % with their glow)
        add(d);
    };
    auto pot = [&](float cx, float cy, float d, const char* param, uint32_t cap) {
        E e;
        e.kind = E::Pot;
        e.x = cx - d / 2, e.y = cy - d / 2, e.w = e.h = d, e.param = param, e.colour = cap, e.style = E::Dial300;
        add(e);
    };
    auto key = [&](float cx, int id, const char* hot) {
        E e;
        e.kind = E::Key;
        e.w = 8.7f, e.h = 17.0f, e.x = cx - e.w / 2, e.y = 66.7f - e.h / 2, e.id = id, e.style = E::DarkSquare, e.hotkey = hot;
        add(e);
    };

    // rack ears and screws
    hole(7.6f, 5.5f, 0), hole(474.2f, 5.5f, 0), hole(7.6f, 81.9f, 0), hole(474.2f, 81.9f, 0);
    hole(32.8f, 7.6f, 1), hole(449.7f, 7.6f, 1), hole(31.7f, 78.7f, 1), hole(449.7f, 78.0f, 1);

    // the model legend (the maker's name above it is left off)
    text(31.3f, 27.8f, "STARGATE 323", 2.6f, E::Left);
    text(31.3f, 33.0f, "DIGITAL REVERBERATOR", 2.6f, E::Left);

    // the display window: smoked glass with round ends, the red filters behind it
    window(101.6f, 13.1f, 432.7f, 36.5f, 0xff141617, 1);
    window(241.5f, 20.8f, 252.4f, 31.0f, 0xff2a0a0a, 0);
    window(279.7f, 20.8f, 310.3f, 31.0f, 0xff2a0a0a, 0);
    window(326.7f, 20.8f, 357.3f, 31.0f, 0xff2a0a0a, 0);
    frame(225.7f, 18.4f, 269.9f, 33.0f, "room");
    frame(273.2f, 18.4f, 316.9f, 33.0f, "ms");
    frame(320.1f, 18.4f, 364.9f, 33.0f, "sec");
    digit(246.9f, 0);
    digit(284.7f, 1), digit(295.0f, 2), digit(305.3f, 3);
    digit(331.7f, 4), digit(342.0f, 5), digit(352.3f, 6);

    // level meter
    text(161.7f, 17.5f, "level", 2.2f);
    const float lx[NumLevelLeds] = {125.7f, 136.1f, 146.4f, 156.7f, 166.7f, 177.0f, 186.8f, 196.7f};
    const char* ln[NumLevelLeds] = {"- 36", "- 30", "- 24", "- 18", "- 12", "- 6", "- 3", "0"};
    for (int i = 0; i < NumLevelLeds; i++) {
        text(lx[i], 22.7f, ln[i], 1.7f);
        led(lx[i], 28.4f, LedLevel0 + i);
    }
    text(213.1f, 22.5f, "dB", 2.2f);
    {
        E u;   // the rule under the scale
        u.kind = E::Path;
        u.x = 122.4f, u.y = 25.45f, u.w = 92.9f, u.h = 0.3f, u.colour = 0xffa8aaa6;
        u.text = "M122.4 25.45 H215.3 V25.75 H122.4 Z";
        add(u);
    }

    // override
    text(391.2f, 21.9f, "override", 2.2f);
    led(375.9f, 27.3f, LedMute), led(391.2f, 27.3f, LedDry), led(406.5f, 27.3f, LedClear);

    // the bracket over the controls
    {
        E b;
        b.kind = E::Path;
        b.x = 31.7f, b.y = 39.85f, b.w = 404.3f, b.h = 3.3f;
        b.text = "M31.7 39.85 H436.0 V43.1 H435.6 V40.25 H32.1 V43.1 H31.7 Z";
        add(b);
    }

    // knobs (blue: level, white: decay EQ) and the red rotary switches, at their shafts: the caps in the
    // photo sit off the shafts (the camera's angle on tall knobs), so the x is where each knob's label
    // and its 0/10 (min/max, -/+) pair centre, which agree to 0.4 mm (re-measured 2026-11-24)
    const uint32_t blue = 0xff2f6fd8, white = 0xffe2e0d8, red = 0xffe8241c;
    text(47.8f, 44.6f, "input");
    pot(47.8f, 61.2f, 21.0f, "inputLevel", blue);
    text(39.3f, 74.7f, "0", 2.2f), text(55.3f, 74.7f, "10", 2.2f);
    text(86.3f, 44.6f, "LF decay");
    pot(85.9f, 61.2f, 21.0f, "lfEq", white);
    text(76.9f, 74.7f, "min", 2.2f), text(94.0f, 74.7f, "max", 2.2f);
    text(124.2f, 44.6f, "HF decay");
    pot(124.2f, 61.2f, 21.0f, "hfDecay", white);
    text(115.8f, 74.7f, "min", 2.2f), text(132.2f, 74.7f, "max", 2.2f);
    text(162.6f, 44.6f, "direct");
    pot(162.6f, 61.2f, 21.0f, "directMix", blue);
    text(154.1f, 74.7f, "0", 2.2f), text(170.9f, 74.7f, "10", 2.2f);
    text(181.9f, 59.0f, "mix");
    text(201.4f, 44.6f, "reverb");
    pot(201.2f, 61.2f, 21.0f, "reverbMix", blue);
    text(193.0f, 74.7f, "0", 2.2f), text(208.7f, 74.7f, "10", 2.2f);

    text(246.3f, 44.6f, "room");
    pot(246.5f, 63.4f, 18.5f, "room", red);
    text(234.9f, 74.7f, "-", 2.4f), text(258.3f, 74.7f, "+", 2.4f);
    text(294.2f, 44.6f, "pre delay");
    pot(294.0f, 63.4f, 18.5f, "predelay", red);
    text(282.6f, 74.7f, "-", 2.4f), text(305.3f, 74.7f, "+", 2.4f);
    text(342.4f, 44.6f, "decay");
    text(342.4f, 48.5f, "time");
    pot(342.3f, 63.4f, 18.5f, "decay", red);
    text(330.6f, 74.7f, "-", 2.4f), text(354.2f, 74.7f, "+", 2.4f);

    // the override keys and power
    text(376.3f, 44.6f, "input"), text(376.3f, 48.5f, "mute");
    text(391.8f, 44.6f, "dry"), text(391.8f, 48.5f, "only");
    text(406.5f, 44.6f, "reverb"), text(406.5f, 48.5f, "clear");
    key(376.3f, KeyInputMute, "m");
    key(391.8f, KeyDryOnly, "d");
    key(407.1f, KeyReverbClear, "c");
    text(437.1f, 48.5f, "power");
    key(437.7f, -1, "");

    L.notes = "The knobs and the red rotary switches are the plugin's controls. input mute and dry only push on "
              "and off; reverb clear clears while held (computer keys m, d, c). The level LEDs are the plugin's "
              "own input meter (0 dB = converter full scale, assumed). The power key is drawn only.";
    return L;
}

}  // namespace sg323plug
