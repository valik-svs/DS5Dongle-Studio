//
// desktop_logic.h - pure Desktop-toggle decisions.
//
// No flash, no USB, no slot_activate. The firmware module and the host test
// both call these so the four cases in the spec stay in one place:
//   game slot  -> remember it, go to Desktop
//   Desktop    -> back to that slot, or to the fallback if RAM was lost
//   Desktop not assigned, or chord empty -> do nothing
//   a held chord fires once, until every button of it has been released
//

#ifndef DS5_BRIDGE_DESKTOP_LOGIC_H
#define DS5_BRIDGE_DESKTOP_LOGIC_H

#include <cstdint>

constexpr uint8_t DESKTOP_SLOT_NONE = 0xFF;

struct DesktopToggleIn {
    uint8_t active;     // DESKTOP_SLOT_NONE when the live config is not a slot
    uint8_t desktop;    // DESKTOP_SLOT_NONE when no slot is marked Desktop
    uint8_t last;       // RAM return target; DESKTOP_SLOT_NONE after reboot
    uint8_t fallback;   // DESKTOP_SLOT_NONE when the user has not chosen one
    uint8_t slot_count;
};

struct DesktopToggleOut {
    bool    activate;
    uint8_t target;
    uint8_t last;       // RAM value after this toggle
};

static inline bool desktop_slot_ok(uint8_t id, uint8_t slot_count) {
    return id != DESKTOP_SLOT_NONE && id < slot_count;
}

// Where Toggle Desktop should go, and what the RAM return slot becomes.
// `last` is updated only when leaving a real non-Desktop slot. A failed
// activation must not commit that update — the caller keeps the old value
// until slot_activate succeeds.
static inline DesktopToggleOut desktop_toggle_plan(const DesktopToggleIn &in) {
    DesktopToggleOut out{false, DESKTOP_SLOT_NONE, in.last};
    if (!desktop_slot_ok(in.desktop, in.slot_count)) return out;

    if (in.active != in.desktop) {
        out.activate = true;
        out.target   = in.desktop;
        if (desktop_slot_ok(in.active, in.slot_count)) out.last = in.active;
        return out;
    }

    uint8_t back = DESKTOP_SLOT_NONE;
    if (desktop_slot_ok(in.last, in.slot_count) && in.last != in.desktop)
        back = in.last;
    else if (desktop_slot_ok(in.fallback, in.slot_count) && in.fallback != in.desktop)
        back = in.fallback;
    if (back == DESKTOP_SLOT_NONE) return out;
    out.activate = true;
    out.target   = back;
    return out;
}

// Chord latch. `pending` is sticky until the caller clears it: a hold that
// has already fired must not raise it again, and a second feed in the same
// press must not either.
struct DesktopChordState {
    bool     latched;
    bool     holding;
    uint32_t hold_start;
    bool     pending;
};

// hold_cs is centiseconds, 0 = fire as soon as the chord is fully down.
// Unlock requires every chord button up. Releasing one and pressing it
// again is still the same press.
static inline void desktop_chord_feed(DesktopChordState &s, uint32_t mask,
                                      uint32_t chord, uint8_t hold_cs,
                                      uint32_t now) {
    if (chord == 0) {
        s.holding = false;
        return;
    }
    const bool any  = (mask & chord) != 0;
    const bool full = (mask & chord) == chord;
    if (!any) {
        s.latched = false;
        s.holding = false;
        return;
    }
    if (!full || s.latched) {
        s.holding = false;
        return;
    }
    if (hold_cs == 0) {
        s.pending = true;
        s.latched = true;
        s.holding = false;
        return;
    }
    if (!s.holding) {
        s.holding    = true;
        s.hold_start = now;
        return;
    }
    const uint32_t need = (uint32_t) hold_cs * 10u;
    if ((uint32_t) (now - s.hold_start) >= need) {
        s.pending = true;
        s.latched = true;
        s.holding = false;
    }
}

#endif
