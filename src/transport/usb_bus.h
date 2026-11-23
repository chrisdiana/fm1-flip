// Known-state handling for the ESP32-S3's native USB pins (GPIO19 = D-, GPIO20 = D+)
// around FM-1 sessions, a presence probe run before the USB host starts, and the
// worker/main-task handshake for the switchable 5 V output.
//
// A session can leave state behind: the boot-key path drives the pins as GPIOs,
// routes D+ to the RMT and detaches the USB-Serial-JTAG pad; the USB host sets
// PHY pull overrides. None of that is cleared by a failed attempt, so the next
// attempt (and a PC connection) start from an unknown state. prepare_for_host()
// and restore_idle() put the pins and PHY back to a defined state and log what
// they had to clear.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "uboot_host.h"

namespace usbbus {

// Before USB host start: no GPIO drive, no RMT routing on the pins, no GPIO
// pulls, no PHY pull overrides or D+/D- exchange. Leaves PHY routing to
// usb_host_install(). Logs any leftover state it cleared.
void prepare_for_host(host::Log log);

// After a session ends or fails (USB host already stopped): the power-on state.
// Pins released, overrides cleared, internal PHY handed back to USB-Serial-JTAG
// with its pad enabled, so the StickS3 reappears on a PC without download mode.
void restore_idle(host::Log log);

enum class Presence { Empty, Device, Unstable, Fault };
// Senses the bus with only weak internal pull-downs (the stick's own pull-ups
// detached) for about 100 ms. Empty: nothing pulls a line up (no device, a
// switched-off FM-1, or one whose battery is too low to run its USB). Device:
// a steady full-speed pull-up on D+. Unstable: the lines toggle or the pull-up
// comes and goes. Fault: both lines high. Must run with the USB host stopped.
Presence probe(host::Log log);
const char *presence_name(Presence p);

}  // namespace usbbus
