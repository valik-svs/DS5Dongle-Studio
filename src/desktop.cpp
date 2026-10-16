//
// desktop.cpp - flash blob, chord latch, and the deferred slot switch.
//
// The blob sits in its own sector below the macro table. It is not part of
// Config_body: Desktop is one device-wide fact, and a slot payload is a
// per-game profile. Virgin flash (every device that has never run this
// firmware) fails the magic check and comes up with Desktop unassigned.
//

#include "desktop.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "flash_map.h"
#include "utils.h"
#include "wake.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/time.h"
#include "tusb.h"

constexpr uint32_t DESKTOP_MAGIC   = 0x44355344u; // "DS5D" little-endian
constexpr uint8_t  DESKTOP_VERSION = 1;
struct __attribute__((packed)) DesktopRecord {
    uint32_t magic;
    uint8_t  version;
    uint8_t  desktop_slot_id;
    uint8_t  fallback_slot_id;
    uint8_t  lightbar_enabled;
    uint8_t  lightbar_r;
    uint8_t  lightbar_g;
    uint8_t  lightbar_b;
    uint8_t  hold_cs;
    uint32_t chord;
    uint32_t crc32;
};
static_assert(sizeof(DesktopRecord) <= 256, "desktop record must fit one flash page");

static DesktopSettings   g_set;
static DesktopChordState g_chord_state;
static bool              g_loaded = false;
static uint32_t          g_last_mask = 0;
// RAM only. A reboot zeros this, which is what makes the return target the
// fallback slot instead of whatever game was active before power was lost.
static uint8_t           g_last_non_desktop = DESKTOP_SLOT_NONE;

static uint32_t record_crc(const DesktopRecord &r) {
    return crc32(reinterpret_cast<const uint8_t *>(&r.version),
                 offsetof(DesktopRecord, crc32) - offsetof(DesktopRecord, version));
}

static void settings_from_record(const DesktopRecord &r) {
    g_set.desktop_slot_id  = r.desktop_slot_id;
    g_set.fallback_slot_id = r.fallback_slot_id;
    g_set.lightbar_enabled = r.lightbar_enabled ? 1 : 0;
    g_set.lightbar_r       = r.lightbar_r;
    g_set.lightbar_g       = r.lightbar_g;
    g_set.lightbar_b       = r.lightbar_b;
    g_set.hold_cs          = r.hold_cs;
    g_set.chord            = r.chord;
}

static void settings_default() {
    memset(&g_set, 0, sizeof(g_set));
    g_set.desktop_slot_id  = DESKTOP_SLOT_NONE;
    g_set.fallback_slot_id = DESKTOP_SLOT_NONE;
}

static bool slot_occupied(uint8_t idx) {
    if (idx >= SLOT_COUNT) return false;
    uint8_t name[SLOT_NAME_LEN];
    uint8_t valid = 0, ver = 0;
    return slot_info(idx, name, valid, ver) && valid != 0;
}

static void flash_op(void *param) {
    const uint8_t *img = static_cast<const uint8_t *>(param);
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(DESKTOP_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(DESKTOP_FLASH_OFFSET, img, 256);
    restore_interrupts(interrupts);
}

static bool save_settings() {
    DesktopRecord rec{};
    rec.magic             = DESKTOP_MAGIC;
    rec.version           = DESKTOP_VERSION;
    rec.desktop_slot_id   = g_set.desktop_slot_id;
    rec.fallback_slot_id  = g_set.fallback_slot_id;
    rec.lightbar_enabled  = g_set.lightbar_enabled;
    rec.lightbar_r        = g_set.lightbar_r;
    rec.lightbar_g        = g_set.lightbar_g;
    rec.lightbar_b        = g_set.lightbar_b;
    rec.hold_cs           = g_set.hold_cs;
    rec.chord             = g_set.chord;
    rec.crc32             = record_crc(rec);

    alignas(4) static uint8_t image[256];
    memset(image, 0xff, sizeof(image));
    memcpy(image, &rec, sizeof(rec));
    const int rc = flash_safe_execute(flash_op, image, 1000);
    if (rc != PICO_OK) {
        printf("[Desktop] save failed: %d\n", rc);
        return false;
    }
    return true;
}

// A deleted Desktop slot must not stay designated. Checked when the blob is
// loaded and when the portal reads it, not on every toggle — that would erase
// a flash sector on a chord.
static void clear_missing_desktop() {
    if (g_set.desktop_slot_id == DESKTOP_SLOT_NONE) return;
    if (slot_occupied(g_set.desktop_slot_id)) return;
    printf("[Desktop] slot %u is gone, clearing designation\n", g_set.desktop_slot_id);
    g_set.desktop_slot_id = DESKTOP_SLOT_NONE;
    save_settings();
}

void desktop_load() {
    if (g_loaded) return;
    settings_default();
    const auto *flashed =
        reinterpret_cast<const DesktopRecord *>(XIP_BASE + DESKTOP_FLASH_OFFSET);
    DesktopRecord rec;
    memcpy(&rec, flashed, sizeof(rec));
    if (rec.magic == DESKTOP_MAGIC && rec.version == DESKTOP_VERSION &&
        rec.crc32 == record_crc(rec)) {
        settings_from_record(rec);
    }
    g_loaded = true;
    clear_missing_desktop();
}

void desktop_boot() {
    desktop_load();
    if (!slot_occupied(g_set.fallback_slot_id)) return;
    // USB has not enumerated yet (the host-facing connect happens later, when
    // the controller links). The descriptors are built from the live config,
    // so a fallback activation here needs no reconnect of its own.
    bool reenum = false;
    uint8_t stage = 0;
    const uint8_t res = slot_activate(g_set.fallback_slot_id, reenum, stage);
    if (res == 0) {
        printf("[Desktop] boot fallback slot %u failed (stage %u)\n",
               g_set.fallback_slot_id, stage);
    }
    (void) reenum;
}

bool desktop_get(DesktopSettings &out) {
    if (!g_loaded) desktop_load();
    clear_missing_desktop();
    out = g_set;
    return true;
}

bool desktop_set(const DesktopSettings &in) {
    if (!g_loaded) desktop_load();
    if (in.desktop_slot_id != DESKTOP_SLOT_NONE && !slot_occupied(in.desktop_slot_id))
        return false;
    if (in.fallback_slot_id != DESKTOP_SLOT_NONE &&
        in.fallback_slot_id >= SLOT_COUNT)
        return false;
    g_set = in;
    g_set.lightbar_enabled = in.lightbar_enabled ? 1 : 0;
    // The portal saves this while the user may still be holding the chord
    // they just recorded. Occupy the latch so that press cannot toggle until
    // they let go.
    desktop_disarm();
    return save_settings();
}

void desktop_on_buttons(uint32_t mask, uint32_t now_ms, bool frozen) {
    if (!g_loaded) desktop_load();
    // `mask` is already the macro engine's debounced, pad-half-qualified mask.
    // A second debounce here would hold the latch 50 ms longer than every
    // other chord in the device. Record mode still has to see the release:
    // forcing the latch shut and ignoring the mask left it shut after the
    // portal stopped recording, so the next press never toggled.
    g_last_mask = mask;
    desktop_chord_feed(g_chord_state, g_last_mask, g_set.chord, g_set.hold_cs, now_ms);
    if (frozen) g_chord_state.pending = false;
}

void desktop_disarm() {
    g_chord_state.latched  = true;
    g_chord_state.holding  = false;
    g_chord_state.pending  = false;
    g_last_mask = 0;
}

static void usb_reenum() {
    wake_note_usb_reconnect();
    tud_disconnect();
    sleep_ms(250);
    tud_connect();
}

void desktop_task() {
    if (!g_loaded) desktop_load();
    // A still chord sends no new reports, so the hold timer has to be checked
    // from here. Mask 0 is not fed: that release has to come from a real
    // report, otherwise desktop_disarm()'s cleared mask would unlock the latch
    // and the chord still under the user's fingers would fire on the next one.
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    // Host-suspend must not pause this. A Desktop switch reconnects USB, and
    // that reconnect is reported as a suspend; skipping the feed then left the
    // latch shut, so the next press did nothing.
    if (g_last_mask != 0) {
        desktop_chord_feed(g_chord_state, g_last_mask, g_set.chord, g_set.hold_cs, now);
    }
    if (!g_chord_state.pending) return;
    g_chord_state.pending = false;

    uint8_t active = active_slot_index();

    DesktopToggleIn in;
    in.active     = active;
    in.desktop    = g_set.desktop_slot_id;
    in.last       = g_last_non_desktop;
    in.fallback   = g_set.fallback_slot_id;
    in.slot_count = SLOT_COUNT;
    const DesktopToggleOut plan = desktop_toggle_plan(in);
    if (!plan.activate) return;
    if (!slot_occupied(plan.target)) {
        printf("[Desktop] target slot %u is empty\n", plan.target);
        return;
    }
    if (plan.target == active) return;

    bool reenum = false;
    uint8_t stage = 0;
    const uint8_t res = slot_activate(plan.target, reenum, stage);
    if (res == 0) {
        printf("[Desktop] activate %u failed (stage %u)\n", plan.target, stage);
        return;
    }
    g_last_non_desktop = plan.last;
    if (reenum) usb_reenum();
}

bool desktop_lightbar_override(uint8_t &r, uint8_t &g, uint8_t &b) {
    if (!g_loaded || !g_set.lightbar_enabled) return false;
    if (g_set.desktop_slot_id == DESKTOP_SLOT_NONE) return false;
    if (active_slot_index() != g_set.desktop_slot_id) return false;
    r = g_set.lightbar_r;
    g = g_set.lightbar_g;
    b = g_set.lightbar_b;
    return true;
}

bool desktop_lightbar_wants_report() {
    uint8_t r, g, b;
    // The controller keeps the last lightbar colour until a later report
    // replaces it. While Desktop is active we keep sending. The moment we
    // leave, one more short run has to go out too: that run no longer carries
    // the Desktop colour, so Lightbar Off (or the game's own colour) actually
    // reaches the pad. Without it the Desktop colour stays until something
    // else, such as a battery-notification test, happens to write the bar.
    static bool was_on = false;
    static uint64_t tail_until_us = 0;
    const bool on = desktop_lightbar_override(r, g, b);
    if (was_on && !on) tail_until_us = time_us_64() + 300000;
    was_on = on;
    return on || time_us_64() < tail_until_us;
}
