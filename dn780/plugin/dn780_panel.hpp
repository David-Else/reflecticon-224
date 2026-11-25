// The DN780 front panel for the kit's hardware mode (kit::PanelLayout), measured from straight-on
// reference photos (the dn780 project's photos/: retrogear for the panel, soundgas lit for the
// digits; local reference only: nothing of them is shipped, this is a vector drawing at their
// positions). 19" 2U, millimetres (5.625 photo px per mm).
//
// The panel is the firmware's own: the keys are the 8279's key codes (they go into its FIFO and
// interrupt the Z80), UP / DOWN and the REV mute are the F001 input bits, the 16 digits are the
// 8279's display RAM. What the firmware does not drive: the level meter (an LM3915 on the input,
// computed by the plugin), the LEVEL knob and the IN mute (analogue, in the plugin).
//
// Ids:
//   keys     the 8279 key code (C0-F4, notes/2026-11-09-2230 §2 and the v2.2 dispatch at 00A8);
//            KeyDown / KeyUp / KeyIn below; 0xEE (REV) is also held on F001 bit 2
//   digits   the 8279 digit number (PRE D,E,F · PAT C · LEV 0 · DECAY 3,1 · LF 2,5 · HF 6,7 ·
//            SIZE B,8 · MEM 9,A); digit 4 drives the indicator LEDs
//   LEDs     0-9 meter 0 ... -27 dB, 10-16 the parameter LEDs (digit 4), 17 Store, 18-20 a b c
#pragma once
#include "../../shared/core/panel.hpp"
#include <cmath>
#include <cstdio>
#include <string>

namespace dn780plug {

// matrix keys: the 8279's code for each (CNTL and SHIFT read high)
enum DnKey {
    KeyPredelay = 0xF2, KeyPattern = 0xF3, KeyLevel = 0xC3, KeyDecay = 0xC1, KeyLF = 0xF1, KeyHF = 0xE1,
    KeyRoom = 0xE3, KeySto = 0xF4, KeySeq = 0xC4, KeyRev = 0xEE,
    Key0 = 0xF0, Key1 = 0xE4, Key2 = 0xE0, Key3 = 0xC0, Key4 = 0xD4, Key5 = 0xD0, Key6 = 0xD2, Key7 = 0xCC,
    Key8 = 0xC8, Key9 = 0xCA,
    KeyDown = 0x100, KeyUp = 0x101, KeyIn = 0x102,   // not matrix keys: F001 bits 0 / 1, the analogue mute
};
enum DnLed { LedMeter0 = 0, NumMeterLeds = 10, LedParam0 = 10, NumParamLeds = 7, LedStore = 17, LedA = 18, LedB = 19, LedC = 20, NumDnLeds = 21 };
// digit 4's bits for the parameter LEDs, in LED order (pre, pattern, level, decay, LF, HF, room)
constexpr uint8_t kParamLedBit[NumParamLeds] = {0x10, 0x04, 0x08, 0x20, 0x40, 0x02, 0x01};

inline kit::PanelLayout dn780_layout() {
    using E = kit::PanelElement;
    kit::PanelLayout L;
    L.width = 483.0f;
    L.height = 88.9f;
    L.face = 0xffcfcfcb;   // brushed aluminium
    L.ink = 0xff161616;
    L.leds = NumDnLeds;
    L.digits = 16;
    auto add = [&](E e) { L.elements.push_back(std::move(e)); };
    auto text = [&](float x, float y, const char* s, float size = 2.1f, int just = E::Centre, uint32_t col = 0) {
        E e;
        e.kind = E::Text;
        e.x = x, e.y = y, e.text = s, e.size = size, e.style = just, e.colour = col;
        add(e);
    };
    auto hole = [&](float cx, float cy, int style) {
        E e;
        e.kind = E::Hole;
        float w = style == 0 ? 10.0f : 4.4f, h = style == 0 ? 6.4f : w;
        e.x = cx - w / 2, e.y = cy - h / 2, e.w = w, e.h = h, e.style = style;
        add(e);
    };
    auto path = [&](const std::string& d, float x0, float y0, float x1, float y1, uint32_t col = 0) {
        E e;
        e.kind = E::Path;
        e.x = x0, e.y = y0, e.w = x1 - x0, e.h = y1 - y0, e.text = d, e.colour = col;
        add(e);
    };
    char buf[256];
    constexpr float lw = 0.35f;   // printed line weight (the photos: ~2 px at 5.6 px/mm)
    auto hline = [&](float x0, float x1, float y, uint32_t col = 0) {
        std::snprintf(buf, sizeof buf, "M%.2f %.2f H%.2f V%.2f H%.2f Z", x0, y - lw / 2, x1, y + lw / 2, x0);
        path(buf, x0, y - lw / 2, x1, y + lw / 2, col);
    };
    auto vline = [&](float x, float y0, float y1, uint32_t col = 0) {
        std::snprintf(buf, sizeof buf, "M%.2f %.2f V%.2f H%.2f V%.2f Z", x - lw / 2, y0, y1, x + lw / 2, y0);
        path(buf, x - lw / 2, y0, x + lw / 2, y1, col);
    };
    // a quarter ring round (cx, cy), radius r: q 0 top-left, 1 top-right, 2 bottom-right, 3 bottom-left
    auto corner = [&](float cx, float cy, float r, int q, uint32_t col = 0) {
        const float ro = r + lw / 2, ri = r - lw / 2;
        const float sx = (q == 1 || q == 2) ? 1.0f : -1.0f, sy = (q >= 2) ? 1.0f : -1.0f;
        // from the horizontal end to the vertical end, outer, then back on the inner arc
        const int sweep_out = (sx * sy < 0) ? 1 : 0;
        std::snprintf(buf, sizeof buf, "M%.3f %.3f A%.3f %.3f 0 0 %d %.3f %.3f L%.3f %.3f A%.3f %.3f 0 0 %d %.3f %.3f Z", cx, cy + sy * ro, ro,
                      ro, sweep_out, cx + sx * ro, cy, cx + sx * ri, cy, ri, ri, 1 - sweep_out, cx, cy + sy * ri);
        path(buf, cx - ro, cy - ro, cx + ro, cy + ro, col);
    };
    // a printed rounded frame; the bottom edge drawn only along `bottom` (pairs of x, NaN-free)
    auto rframe = [&](float x0, float y0, float x1, float y1, float r, std::initializer_list<float> bottom, uint32_t col = 0) {
        hline(x0 + r, x1 - r, y0, col);
        vline(x0, y0 + r, y1 - r, col), vline(x1, y0 + r, y1 - r, col);
        corner(x0 + r, y0 + r, r, 0, col), corner(x1 - r, y0 + r, r, 1, col);
        corner(x1 - r, y1 - r, r, 2, col), corner(x0 + r, y1 - r, r, 3, col);
        const float* b = bottom.begin();
        if (bottom.size() == 0) hline(x0 + r, x1 - r, y1, col);
        for (size_t i = 0; i + 1 < bottom.size(); i += 2) hline(b[i], b[i + 1], y1, col);
    };
    auto rect = [&](float x0, float y0, float x1, float y1, uint32_t col) {
        std::snprintf(buf, sizeof buf, "M%.2f %.2f H%.2f V%.2f H%.2f Z", x0, y0, x1, y1, x0);
        path(buf, x0, y0, x1, y1, col);
    };
    auto window = [&](float x0, float y0, float x1, float y1, uint32_t tint) {
        E e;
        e.kind = E::Window;
        e.x = x0, e.y = y0, e.w = x1 - x0, e.h = y1 - y0, e.colour = tint;
        add(e);
    };
    auto led = [&](float cx, float cy, int id, uint32_t col, float d = 1.8f) {
        E e;
        e.kind = E::Led;
        e.x = cx - d / 2, e.y = cy - d / 2, e.w = e.h = d, e.id = id, e.colour = col;
        add(e);
    };
    const float dig_y = 32.9f;
    auto digit = [&](float cx, int id, uint32_t col, int style = E::Seven) {
        window(cx - 4.7f, dig_y - 6.4f, cx + 4.7f, dig_y + 6.4f, col == 0xffff3434 ? 0xff200a0a : 0xff1e1a10);
        E d;
        d.kind = E::Digit;
        d.w = 7.0f, d.h = 10.9f, d.x = cx - d.w / 2, d.y = dig_y - d.h / 2, d.id = id, d.colour = col, d.style = style;
        d.size = 1.9f;   // ~11 % of the height (the lit photo)
        add(d);
    };
    auto key = [&](float cx, float cy, int id, const char* legend, const char* hot, float w = 16.4f, float h = 8.2f) {
        E e;
        e.kind = E::Key;
        e.w = w, e.h = h, e.x = cx - w / 2, e.y = cy - h / 2, e.id = id, e.style = E::DarkSquare, e.text = legend,
        e.colour = 0xfff4f4f0, e.hotkey = hot;
        add(e);
    };

    // rack ears and screws
    hole(7.9f, 6.1f, 0), hole(475.1f, 6.1f, 0), hole(7.9f, 82.4f, 0), hole(475.1f, 82.4f, 0);
    hole(31.5f, 11.9f, 1), hole(451.1f, 12.3f, 1), hole(31.3f, 76.4f, 1), hole(451.3f, 76.1f, 1), hole(229.7f, 76.4f, 1);

    // the model legend (the maker's name and logo to its left are left off)
    text(231.8f, 8.0f, "DN 780   DIGITAL REVERBERATOR / PROCESSOR", 2.6f);

    // printed frames: the outer one, AUDIO, the display and keys, MEMORY; all four corners ~4.1 mm in
    // both photos (fitted on the diagonals)
    constexpr float kR = 4.1f;
    rframe(39.5f, 12.7f, 443.2f, 75.1f, kR, {});
    rframe(48.8f, 16.2f, 102.4f, 72.0f, kR, {52.9f, 68.6f, 81.4f, 98.3f});
    text(75.0f, 72.0f, "AUDIO", 2.1f);
    rframe(105.3f, 16.0f, 370.9f, 71.8f, kR, {109.4f, 145.3f, 361.9f, 366.8f});
    rframe(373.7f, 15.9f, 430.8f, 71.4f, kR, {377.8f, 394.4f, 410.4f, 426.7f});
    text(402.4f, 71.4f, "MEMORY", 2.1f);
    // the brackets under the keys
    auto bracket = [&](float x0, float x1, float t0, float t1, const char* caption, float cx) {
        vline(x0, 69.0f, 71.8f + lw / 2), vline(x1, 69.0f, 71.8f + lw / 2);
        hline(x0, t0, 71.8f), hline(t1, x1, 71.8f);
        text(cx, 71.7f, caption, 2.1f);
    };
    bracket(156.7f, 194.4f, 162.9f, 187.6f, "REFLECTIONS", 175.3f);
    bracket(205.9f, 274.1f, 225.4f, 253.9f, "REVERBERATION", 239.8f);
    hline(290.8f, 307.5f, 71.8f);
    bracket(321.6f, 358.9f, 329.4f, 350.3f, "PARAMETER", 340.0f);

    // AUDIO: the LM3915 meter, the input LEVEL knob, the mutes
    rect(53.6f, 17.7f, 60.0f, 70.2f, 0xff1c1c1c);
    hline(60.0f, 67.5f, 17.7f), hline(60.0f, 67.5f, 70.2f), vline(67.5f, 17.7f, 70.2f);
    const char* mdb[NumMeterLeds] = {"0", "3", "6", "9", "12", "15", "18", "21", "24", "27"};
    for (int i = 0; i < NumMeterLeds; i++) {
        const float y = 21.3f + 5.12f * float(i);
        led(57.4f, y, LedMeter0 + i, i == 0 ? 0xffff3a2a : i < 3 ? 0xffffc828 : 0xff5ee05a, 3.0f);
        rect(61.7f, y - 0.4f, 62.9f, y + 0.4f, 0);
        text(63.4f, y, mdb[i], 2.0f, E::Left);
    }
    text(63.6f, 23.8f, "dB", 1.7f, E::Left);
    {
        E k;   // the input level: a silver cap on a black skirt
        k.kind = E::Pot;
        const float d = 13.7f;
        k.x = 82.1f - d / 2, k.y = 31.4f - d / 2, k.w = k.h = d, k.param = "inputLevel", k.colour = 0xffc8c8c4,
        k.style = E::Dial300, k.text = " ";
        add(k);
    }
    {   // the printed arc round it, -inf to +6 (225 to 135 degrees from 12 o'clock)
        const float cx = 82.1f, cy = 31.4f, ro = 9.7f, ri = 9.25f, a0 = -2.356f, a1 = 2.356f;
        std::snprintf(buf, sizeof buf, "M%.3f %.3f A%.3f %.3f 0 1 1 %.3f %.3f L%.3f %.3f A%.3f %.3f 0 1 0 %.3f %.3f Z", cx + ro * std::sin(a0),
                      cy - ro * std::cos(a0), ro, ro, cx + ro * std::sin(a1), cy - ro * std::cos(a1), cx + ri * std::sin(a1),
                      cy - ri * std::cos(a1), ri, ri, cx + ri * std::sin(a0), cy - ri * std::cos(a0));
        path(buf, cx - ro, cy - ro, cx + ro, cy + ro);
    }
    text(82.5f, 19.8f, "0dB", 2.0f);
    vline(82.3f, 21.2f, 22.6f);
    hline(69.0f, 71.2f, 31.4f), hline(93.4f, 95.6f, 31.4f);
    text(75.8f, 41.7f, "- \xe2\x88\x9e", 2.0f), text(87.8f, 41.7f, "+ 6", 2.0f);
    {
        const char* lv = "LEVEL";
        for (int i = 0; i < 5; i++) text(97.9f, 26.1f + 2.65f * float(i), std::string(1, lv[i]).c_str(), 2.0f);
        const char* mu = "MUTES";
        for (int i = 0; i < 5; i++) text(97.7f, 51.3f + 2.7f * float(i), std::string(1, mu[i]).c_str(), 2.0f);
    }
    hline(93.0f, 97.9f, 48.6f), vline(97.9f, 48.6f, 50.0f);
    hline(93.0f, 97.9f, 65.3f), vline(97.9f, 63.9f, 65.3f);
    key(82.7f, 50.7f, KeyIn, "IN", "i");
    key(82.7f, 63.6f, KeyRev, "REV", "v");

    // the display window: 16 digits, the parameter LEDs, a b c, Store
    window(109.5f, 20.9f, 358.9f, 51.7f, 0xff0c0c0d);
    const uint32_t green = 0xffa8e83a, red = 0xffff3434, white = 0xfff4f4f0, lred = 0xffff3424;
    digit(125.6f, 13, green), digit(135.6f, 14, green), digit(145.3f, 15, green);
    digit(166.1f, 12, green);
    digit(186.9f, 0, green);
    digit(208.2f, 3, green), digit(217.9f, 1, green);
    digit(238.5f, 2, green, E::Sign), digit(248.3f, 5, green);
    digit(263.9f, 6, green, E::Sign), digit(273.9f, 7, green);
    digit(294.6f, 11, green), digit(304.4f, 8, green);
    digit(334.8f, 9, red), digit(344.6f, 10, red);
    auto sub = [&](float x, const char* n) {   // f with a subscript
        text(x - 0.5f, 36.6f, "f", 2.5f, E::Centre, white);
        text(x + 0.8f, 37.6f, n, 1.6f, E::Centre, white);
    };
    text(154.3f, 36.8f, "mS.", 2.1f, E::Centre, white);
    sub(173.4f, "1"), sub(194.6f, "2");
    text(226.9f, 36.8f, "Sec.", 2.1f, E::Centre, white);
    sub(255.0f, "3"), sub(282.0f, "4");
    text(313.9f, 36.8f, "m.", 2.1f, E::Centre, white);
    text(355.0f, 37.5f, "Mem.", 2.1f, E::Centre, white);
    const float pled[NumParamLeds] = {135.6f, 166.1f, 186.9f, 213.0f, 243.4f, 268.9f, 299.5f};
    for (int i = 0; i < NumParamLeds; i++) led(pled[i], 47.6f, LedParam0 + i, lred, 1.6f);
    text(314.8f, 47.0f, "a", 1.9f, E::Centre, white), text(320.0f, 47.0f, "b", 1.9f, E::Centre, white),
        text(325.2f, 47.0f, "c", 1.9f, E::Centre, white);
    led(314.8f, 49.9f, LedA, lred, 1.4f), led(320.0f, 49.9f, LedB, lred, 1.4f), led(325.2f, 49.9f, LedC, lred, 1.4f);
    text(340.9f, 46.1f, "Store \xe2\x80\x93", 2.0f, E::Centre, white);
    led(349.2f, 46.1f, LedStore, lred, 1.6f);

    // the parameter keys
    const float ky = 63.45f, ly = 55.5f;
    struct K {
        float x;
        int id;
        const char *label, *legend, *hot;
    } keys[] = {
        {134.15f, KeyPredelay, "PREDELAY", "", "p"}, {165.15f, KeyPattern, "PATTERN", "", "t"},
        {185.75f, KeyLevel, "LEVEL", "", "l"},       {214.05f, KeyDecay, "DECAY", "", "d"},
        {245.05f, KeyLF, "LF", "", "f"},             {265.65f, KeyHF, "HF", "", "h"},
        {299.05f, KeyRoom, "ROOM SIZE", "", "r"},    {329.75f, KeyDown, "DOWN", "\xe2\x80\x93", "DOWN"},
        {350.2f, KeyUp, "UP", "+", "UP"},
    };
    for (auto& k : keys) {
        text(k.x, ly, k.label, 2.1f);
        key(k.x, ky, k.id, k.legend, k.hot);
    }

    // MEMORY: the keypad
    const float cx[3] = {385.7f, 402.4f, 419.1f}, ry[4] = {25.05f, 37.88f, 50.72f, 63.55f};
    struct P {
        int id;
        const char *legend, *hot;
    } pad[12] = {{Key7, "7", "7"}, {Key8, "8", "8"},   {Key9, "9", "9"}, {Key4, "4", "4"}, {Key5, "5", "5"},     {Key6, "6", "6"},
                 {Key1, "1", "1"}, {Key2, "2", "2"},   {Key3, "3", "3"}, {KeySto, "STO", "s"}, {Key0, "0", "0"}, {KeySeq, "SEQ", "q"}};
    for (int i = 0; i < 12; i++) key(cx[i % 3], ry[i / 3], pad[i].id, pad[i].legend, pad[i].hot, 15.4f, 8.2f);

    // the power switch (drawn only)
    window(436.7f, 58.4f, 451.6f, 64.4f, 0xffd82a20);

    // the software card
    const uint32_t card = 0xff3a342c;
    // two printed lines in the same ink: the outer one round ~6 mm corners, the inner ~1.9 mm inside it
    // (~4 mm corners), both measured on the full-panel photo
    constexpr float kCardGap = 1.9f;   // the same on all four sides, concentric corners
    rframe(453.1f, 13.3f, 480.9f, 72.7f, 6.0f, {}, card);
    rframe(453.1f + kCardGap, 13.3f + kCardGap, 480.9f - kCardGap, 72.7f - kCardGap, 6.0f - kCardGap, {}, card);
    hline(460.1f, 473.9f, 21.75f, card);   // SOFTWARE and NOTE are underlined
    text(467.0f, 17.4f, "SERIES 2.0", 1.9f, E::Centre, card);
    text(467.0f, 20.3f, "SOFTWARE", 1.9f, E::Centre, card);
    const char* lines[] = {"01-05 Halls",     "06-10 Plates",    "11-15 Chambers", "16-20 Rooms",          "21-25 Spare",
                           "26 \xe2\x80\x94 Alive", "27 \xe2\x80\x94 Non Lin", "28 \xe2\x80\x94 Reverse", "29 \xe2\x80\x94 Spare",
                           "30 \xe2\x80\x94 Spare", "31 \xe2\x80\x94 Delay", "32 \xe2\x80\x94 ADT",   "33 \xe2\x80\x94 Multitap Echo",
                           "34 \xe2\x80\x94 S.O.S.*", "35 \xe2\x80\x94 Infinite Room*", "36-39 Spare", "40-89 User Mem."};
    for (int i = 0; i < 17; i++) text(456.1f, 23.3f + 2.27f * float(i), lines[i], 1.5f, E::Left, card);
    text(467.0f, 62.3f, "NOTE", 1.6f, E::Centre, card);
    hline(464.0f, 470.0f, 63.45f, card);
    text(456.1f, 65.1f, "(1) \xe2\x80\x94 *Use 'Level'", 1.5f, E::Left, card);
    text(456.1f, 67.3f, "(2) \xe2\x80\x94 Press 0,0 For", 1.5f, E::Left, card);
    text(462.6f, 69.4f, "Mem. No.", 1.5f, E::Left, card);

    L.notes = "The keys go to the unit's own firmware (keyboard chip and interrupt, as on the unit): select a "
              "parameter and step it with DOWN / UP; type a memory number on the keypad; STO then a number "
              "stores. REV mutes the reverb while held. The meter, LEVEL and IN (input mute, held) are the "
              "plugin's own. Memories 34 and 35 (S.O.S., Infinite Room) are not supported. Computer keys: "
              "digits, arrows, p t l d f h r (parameters), s q (STO, SEQ), i v (mutes).";
    return L;
}

}  // namespace dn780plug
