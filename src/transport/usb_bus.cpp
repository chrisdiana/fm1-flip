#include "usb_bus.h"

#include <atomic>
#include <initializer_list>
#include <stdio.h>
#include <string.h>

#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/usb_serial_jtag_ll.h>
#include <soc/gpio_reg.h>
#include <soc/gpio_sig_map.h>
#include <soc/io_mux_reg.h>
#include <soc/rtc_cntl_struct.h>

namespace usbbus {

static constexpr gpio_num_t PIN_DM = GPIO_NUM_19;
static constexpr gpio_num_t PIN_DP = GPIO_NUM_20;
static constexpr uint32_t PIN_MASK = (1u << PIN_DM) | (1u << PIN_DP);

static uint32_t out_sel(gpio_num_t pin) { return REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + 4 * pin) & 0x1FF; }
static uint32_t iomux_reg(gpio_num_t pin) { return pin == PIN_DP ? IO_MUX_GPIO20_REG : IO_MUX_GPIO19_REG; }

// Appends a description of anything that differs from an idle, undriven bus.
static void describe_leftovers(char *out, size_t n) {
    size_t k = 0;
    out[0] = 0;
    auto add = [&](const char *s) {
        if (k < n) k += snprintf(out + k, n - k, "%s%s", k ? ", " : "", s);
    };
    uint32_t enabled = REG_READ(GPIO_ENABLE_REG) & PIN_MASK;
    if (enabled & (1u << PIN_DP)) add("D+ GPIO output on");
    if (enabled & (1u << PIN_DM)) add("D- GPIO output on");
    if (out_sel(PIN_DP) != SIG_GPIO_OUT_IDX) add("D+ routed to a peripheral");
    if (out_sel(PIN_DM) != SIG_GPIO_OUT_IDX) add("D- routed to a peripheral");
    for (gpio_num_t pin : {PIN_DP, PIN_DM}) {
        uint32_t r = REG_READ(iomux_reg(pin));
        if (r & FUN_PU) add(pin == PIN_DP ? "D+ GPIO pull-up" : "D- GPIO pull-up");
        if (r & FUN_PD) add(pin == PIN_DP ? "D+ GPIO pull-down" : "D- GPIO pull-down");
    }
    if (USB_SERIAL_JTAG.conf0.exchg_pins_override) add("USJ D+/D- exchange");
    if (USB_SERIAL_JTAG.conf0.pad_pull_override) add("USJ pull override");
    if (!USB_SERIAL_JTAG.conf0.usb_pad_enable) add("USJ pad disabled");
}

// GPIO side only: no drive, no peripheral routing, no pulls; input buffers on so
// GPIO_IN reflects the pads for logging. The pin function (USB pad) is unchanged.
static void release_pins() {
    REG_WRITE(GPIO_ENABLE_W1TC_REG, PIN_MASK);
    REG_WRITE(GPIO_OUT_W1TC_REG, PIN_MASK);
    for (gpio_num_t pin : {PIN_DP, PIN_DM}) {
        esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
        gpio_pullup_dis(pin);
        gpio_pulldown_dis(pin);
        PIN_INPUT_ENABLE(iomux_reg(pin));
    }
    REG_WRITE(GPIO_ENABLE_W1TC_REG, PIN_MASK);
}

void prepare_for_host(host::Log log) {
    char left[200];
    describe_leftovers(left, sizeof left);
    release_pins();
    usb_serial_jtag_ll_phy_enable_pin_exchg(false);
    log("USB pins before host: %s", left[0] ? left : "clean");
}

void restore_idle(host::Log log) {
    char left[200];
    describe_leftovers(left, sizeof left);
    release_pins();
    usb_serial_jtag_ll_phy_enable_pin_exchg(false);
    usb_serial_jtag_ll_phy_disable_pull_override();
    // Power-on pull values (CONF0 reset 0x4200: D+ pull-up only). The boot key's
    // override wrote zeros here; with them the stick would not reappear on a PC.
    USB_SERIAL_JTAG.conf0.dp_pullup = 1;
    USB_SERIAL_JTAG.conf0.dp_pulldown = 0;
    USB_SERIAL_JTAG.conf0.dm_pullup = 0;
    USB_SERIAL_JTAG.conf0.dm_pulldown = 0;
    usb_serial_jtag_ll_phy_enable_external(false);  // internal PHY -> USB-Serial-JTAG
    usb_serial_jtag_ll_phy_enable_pad(true);
    log("USB pins restored to idle (USB-Serial-JTAG owns the PHY); cleared: %s", left[0] ? left : "nothing");
}

const char *presence_name(Presence p) {
    switch (p) {
    case Presence::Empty: return "no pull-up (empty port, FM-1 off, or battery too low for USB)";
    case Presence::Device: return "device pull-up present";
    case Presence::Unstable: return "unstable (lines toggling or pull-up dropping)";
    case Presence::Fault: return "both lines high (short or wiring fault)";
    }
    return "?";
}

Presence probe(host::Log log) {
    // Hand the PHY to USB-Serial-JTAG and switch its pad and pull-ups off, so the
    // stick's own 1.5 kOhm D+ pull-up is not what we measure.
    usb_serial_jtag_ll_phy_enable_external(false);
    usb_serial_jtag_pull_override_vals_t off = {};
    usb_serial_jtag_ll_phy_enable_pull_override(&off);
    usb_serial_jtag_ll_phy_enable_pad(false);
    release_pins();
    gpio_pulldown_en(PIN_DP);  // ~45 kOhm: a device's 1.5 kOhm pull-up still wins
    gpio_pulldown_en(PIN_DM);
    vTaskDelay(pdMS_TO_TICKS(5));

    static constexpr int N = 20000;  // ~100 ms at 5 us spacing
    uint32_t count[4] = {}, changes = 0;
    int last = -1;
    for (int i = 0; i < N; i++) {
        uint32_t in = REG_READ(GPIO_IN_REG);
        int s = ((in >> PIN_DP) & 1) | (((in >> PIN_DM) & 1) << 1);  // 0 SE0, 1 J, 2 K, 3 SE1
        count[s]++;
        if (last >= 0 && s != last) changes++;
        last = s;
        esp_rom_delay_us(5);
    }
    gpio_pulldown_dis(PIN_DP);
    gpio_pulldown_dis(PIN_DM);

    const uint32_t one_pct = N / 100, most = N * 95 / 100;
    Presence p;
    if (count[3] > one_pct) p = Presence::Fault;
    // A healthy FM-1 showed ~35 K samples in short blips at attach (session-10),
    // so only sustained toggling counts: both J and K above 1 %, or hundreds of edges.
    else if ((count[1] > one_pct && count[2] > one_pct) || changes > 200) p = Presence::Unstable;
    else if (count[1] >= most || count[2] >= most) p = Presence::Device;
    else if (count[0] >= most) p = Presence::Empty;
    // A single clean edge: a device attaching (or detaching) during the window.
    else if (changes <= 2 && count[3] == 0) p = (last == 1 || last == 2) ? Presence::Device : Presence::Empty;
    else p = Presence::Unstable;
    log("USB probe: %s (SE0 %lu J %lu K %lu SE1 %lu, %lu changes)", presence_name(p), (unsigned long)count[0],
        (unsigned long)count[1], (unsigned long)count[2], (unsigned long)count[3], (unsigned long)changes);
    return p;
}

static std::atomic<bool> controller(false), wanted(false), applied(false);

void set_power_controller(bool available) { controller.store(available); }
bool power_wanted() { return wanted.load(); }
void power_applied(bool on) { applied.store(on); }

bool request_power(bool on, uint32_t timeout_ms) {
    if (!controller.load()) return true;
    const bool change = applied.load() != on;
    wanted.store(on);
    TickType_t t0 = xTaskGetTickCount();
    while (applied.load() != on) {
        if (xTaskGetTickCount() - t0 > pdMS_TO_TICKS(timeout_ms)) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // Give the rail, and an attached FM-1 that senses VBUS, time to settle.
    if (change) vTaskDelay(pdMS_TO_TICKS(on ? 300 : 50));
    return true;
}

}  // namespace usbbus
