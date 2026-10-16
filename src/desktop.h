//
// desktop.h - device-global Desktop slot toggle.
//
// One slot may be marked Desktop. A chord (stored here, not in the per-slot
// macro table) swaps the live slot with that one. The slot we left is kept
// in RAM only, so a reboot always starts from the chosen fallback slot and
// never resumes a Desktop session.
//
// The chord handler only raises a pending flag. slot_activate() runs from
// desktop_task() on the main loop, because it erases flash and may reconnect
// USB — neither belongs in the BT input callback.
//

#ifndef DS5_BRIDGE_DESKTOP_H
#define DS5_BRIDGE_DESKTOP_H

#include <cstdint>

#include "desktop_logic.h"

struct DesktopSettings {
    uint8_t  desktop_slot_id;   // DESKTOP_SLOT_NONE = not assigned
    uint8_t  fallback_slot_id;  // DESKTOP_SLOT_NONE = do not force a slot at boot
    uint8_t  lightbar_enabled;
    uint8_t  lightbar_r;
    uint8_t  lightbar_g;
    uint8_t  lightbar_b;
    uint8_t  hold_cs;           // 0 = fire on press; otherwise centiseconds
    uint32_t chord;             // logical button mask; 0 = unbound
};

void desktop_load();
void desktop_boot();            // after config_load, before the main loop

// `frozen` is the portal's record mode: keep tracking the release so the
// latch can open, but drop any pending toggle. Host suspend must NOT use
// this. A slot switch re-enumerates USB, the wake logic then reports the
// host as suspended, and freezing the chord there ate every press after
// the first one.
void desktop_on_buttons(uint32_t mask, uint32_t now_ms, bool frozen);

// Main loop. Performs a pending slot_activate and, when the descriptor
// changed, the same USB reconnect as HID command 0x03.
void desktop_task();

// Drop a pending toggle and occupy the latch. Used when the portal saves a
// new chord while the user may still be holding it.
void desktop_disarm();

bool desktop_get(DesktopSettings &out);
bool desktop_set(const DesktopSettings &in);

// True while the Desktop slot is live and its lightbar override is enabled.
// Battery notification is applied after this and wins.
bool desktop_lightbar_override(uint8_t &r, uint8_t &g, uint8_t &b);
// Keep composing an output report so the colour reaches an idle controller.
bool desktop_lightbar_wants_report();

#endif
