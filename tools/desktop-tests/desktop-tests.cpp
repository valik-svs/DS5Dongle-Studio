// Host-side checks for the Desktop toggle decision and the chord latch.
// Build: c++ -std=c++17 -I../../src desktop-tests.cpp -o desktop-tests && ./desktop-tests

#include "desktop_logic.h"

#include <cstdio>
#include <cstdlib>

static int g_fail = 0;

static void expect(bool cond, const char *msg) {
    if (cond) return;
    std::printf("FAIL %s\n", msg);
    g_fail++;
}

static DesktopToggleIn base() {
    DesktopToggleIn in{};
    in.active = 2;
    in.desktop = 5;
    in.last = DESKTOP_SLOT_NONE;
    in.fallback = 0;
    in.slot_count = 32;
    return in;
}

static void test_plan() {
    DesktopToggleIn in = base();
    DesktopToggleOut o = desktop_toggle_plan(in);
    expect(o.activate && o.target == 5 && o.last == 2, "game slot goes to desktop and remembers it");

    in.active = 5;
    in.last = 2;
    o = desktop_toggle_plan(in);
    expect(o.activate && o.target == 2 && o.last == 2, "desktop returns to the remembered slot");

    in.last = DESKTOP_SLOT_NONE;
    o = desktop_toggle_plan(in);
    expect(o.activate && o.target == 0, "reboot: no RAM memory, return is the fallback");

    in.active = 0;
    in.last = DESKTOP_SLOT_NONE;
    o = desktop_toggle_plan(in);
    expect(o.activate && o.target == 5 && o.last == 0, "fallback slot itself can enter desktop");

    in = base();
    in.desktop = DESKTOP_SLOT_NONE;
    o = desktop_toggle_plan(in);
    expect(!o.activate, "unassigned desktop does nothing");

    in = base();
    in.active = DESKTOP_SLOT_NONE;
    o = desktop_toggle_plan(in);
    expect(o.activate && o.target == 5 && o.last == DESKTOP_SLOT_NONE,
           "unknown active still enters desktop without storing 0xFF");

    in = base();
    in.active = 5;
    in.last = DESKTOP_SLOT_NONE;
    in.fallback = 5;
    o = desktop_toggle_plan(in);
    expect(!o.activate, "fallback equal to desktop is not a return target");

    in = base();
    in.active = 5;
    in.last = 5;
    in.fallback = 0;
    o = desktop_toggle_plan(in);
    expect(o.activate && o.target == 0, "a last value that is the desktop slot is ignored");
}

static void test_chord() {
    const uint32_t chord = (1u << 16) | (1u << 13); // PS + Options
    DesktopChordState s{};

    desktop_chord_feed(s, chord, chord, 0, 0);
    expect(s.pending && s.latched, "press fires once");
    s.pending = false;
    desktop_chord_feed(s, chord, chord, 0, 10);
    expect(!s.pending && s.latched, "holding does not fire again");
    desktop_chord_feed(s, 1u << 16, chord, 0, 20);
    expect(!s.pending && s.latched, "partial release stays locked");
    desktop_chord_feed(s, chord, chord, 0, 30);
    expect(!s.pending && s.latched, "re-completing the chord stays locked");
    desktop_chord_feed(s, 0, chord, 0, 40);
    expect(!s.latched, "full release unlocks");
    desktop_chord_feed(s, chord, chord, 0, 50);
    expect(s.pending, "the next press fires");

    s = DesktopChordState{};
    desktop_chord_feed(s, chord, 0, 0, 0);
    expect(!s.pending, "unbound chord does nothing");

    s = DesktopChordState{};
    desktop_chord_feed(s, chord, chord, 75, 1000);
    expect(!s.pending && s.holding, "hold timer arms instead of firing");
    desktop_chord_feed(s, chord, chord, 75, 1000 + 749);
    expect(!s.pending, "hold has not elapsed");
    desktop_chord_feed(s, chord, chord, 75, 1000 + 750);
    expect(s.pending && s.latched && !s.holding, "hold fires once at the threshold");
    s.pending = false;
    desktop_chord_feed(s, chord, chord, 75, 1000 + 2000);
    expect(!s.pending, "staying past the threshold does not repeat");

    s = DesktopChordState{};
    desktop_chord_feed(s, chord, chord, 75, 0);
    desktop_chord_feed(s, 1u << 16, chord, 75, 100);
    expect(!s.holding && !s.latched && !s.pending, "breaking the chord before the timer cancels it");
    desktop_chord_feed(s, chord, chord, 75, 200);
    expect(s.holding && !s.pending, "a new hold can start without a full release before the first fire");
}

int main() {
    test_plan();
    test_chord();
    if (g_fail) {
        std::printf("%d failed\n", g_fail);
        return 1;
    }
    std::printf("desktop-tests ok\n");
    return 0;
}
