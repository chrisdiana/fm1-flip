// USB host + Bulk-Only Transport, after M-Flasher's src/usb.rs (MIT).
#include "uboot_host.h"
#include <fm1/midi.hpp>

#include <initializer_list>
#include <atomic>
#include <string.h>
#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <usb/usb_host.h>
#include <esp_log.h>
#include <soc/gpio_reg.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/usb_serial_jtag_reg.h>
#include <soc/usb_wrap_reg.h>

namespace host {

// Must match the rebuilt IDF enumeration driver, not an app-only -D override.
static_assert(CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE >= 1024,
              "Build with custom_sdkconfig USB control transfer size >= 1024");

static constexpr uint32_t CBW_SIGNATURE = 0x43425355;  // "USBC"
static constexpr uint32_t CSW_SIGNATURE = 0x53425355;  // "USBS"
static constexpr uint32_t TIMEOUT_MS = 5000;           // M-Flasher: 10 s; a 64 KiB erase takes well under 1 s
static constexpr size_t OUT_SIZE = 4096;               // CBW or up to 4 KiB of data
static constexpr size_t IN_SIZE = 4096;                // matches streamed app sectors; avoids 12 KiB idle USB allocation

static usb_host_client_handle_t client;
static usb_device_handle_t dev;
static std::atomic<usb_device_handle_t> claimed_device{nullptr};
static std::atomic<int> new_addr{-1};
static std::atomic<bool> gone{false};
static std::atomic<bool> lib_running{false}, client_running{false};
static std::atomic<bool> lib_stop{false}, client_stop{false}, all_free{false};
static std::atomic<bool> pending_out{false}, pending_in{false}, pending_ctl{false};
static std::atomic<bool> executing{false}, io_fault{false};
static bool installed, ready, claimed, cleanup_failed;
static vprintf_like_t previous_log;
static constexpr uint32_t CLEANUP_MS = 3000;
static uint8_t ep_in, ep_out, iface, alternate;
static bool midi_interface;
static uint16_t mps_in = 64;
static usb_transfer_t *x_out, *x_in, *x_ctl;
static SemaphoreHandle_t done;
static uint32_t tag;
static uint8_t dev_addr;
static Log trace;  // the app's log, for every transfer that is not plainly OK

// ---------------------------------------------------------------- diagnostics

// ESP-IDF's log lines (errors only in this build) kept for the app's log:
// they may come from any task, the SD card is written from the main one.
static portMUX_TYPE idf_mux = portMUX_INITIALIZER_UNLOCKED;
static char idf_buf[8192];
static size_t idf_len;

static int idf_vprintf(const char *fmt, va_list ap) {
    char line[160];
    int n = vsnprintf(line, sizeof line, fmt, ap);
    if (n <= 0) return n;
    size_t k = (size_t)n < sizeof line ? n : sizeof line - 1;
    portENTER_CRITICAL_SAFE(&idf_mux);
    if (idf_len + k < sizeof idf_buf) {
        memcpy(idf_buf + idf_len, line, k);
        idf_len += k;
    }
    portEXIT_CRITICAL_SAFE(&idf_mux);
    return n;
}

static void drain_idf_log(Log log) {
    static char copy[sizeof idf_buf + 1];
    portENTER_CRITICAL(&idf_mux);
    size_t n = idf_len;
    memcpy(copy, idf_buf, n);
    idf_len = 0;
    portEXIT_CRITICAL(&idf_mux);
    copy[n] = 0;
    for (char *line = strtok(copy, "\r\n"); line; line = strtok(nullptr, "\r\n")) log("IDF: %s", line);
}

static constexpr uint32_t OTG_GINTSTS = 0x60080014;
static constexpr uint32_t OTG_HPRT = 0x60080440;

// The root port as the OTG controller sees it, and the pads as the GPIOs see them.
static void port_state(char *out, size_t n) {
    uint32_t h = REG_READ(OTG_HPRT), in = REG_READ(GPIO_IN_REG);
    snprintf(out, n, "port conn%lu ena%lu pwr%lu line D+%lu D-%lu spd%lu | pads D+%d D-%d",
             (unsigned long)(h & 1), (unsigned long)((h >> 2) & 1), (unsigned long)((h >> 12) & 1),
             (unsigned long)((h >> 10) & 1), (unsigned long)((h >> 11) & 1), (unsigned long)((h >> 17) & 3),
             (in >> 20) & 1, (in >> 19) & 1);
}

// Host channels in use: characteristics (odd-frame bit masked), transfer size
// and pending interrupts. Changes show whether a transfer moves at all.
static void channel_state(char *out, size_t n) {
    size_t k = 0;
    out[0] = 0;
    for (int ch = 0; ch < 8 && k < n; ch++) {
        uint32_t base = 0x60080500 + ch * 0x20;
        uint32_t c = REG_READ(base);
        if (!(c & 0x80000000)) continue;  // ChEna
        k += snprintf(out + k, n - k, "ch%d %08lX %08lX %03lX ", ch, (unsigned long)(c & ~(1ul << 29)),
                      (unsigned long)REG_READ(base + 0x10), (unsigned long)(REG_READ(base + 0x08) & 0x7FF));
    }
    if (!out[0]) snprintf(out, n, "no channel active");
}

static void log_phy(Log log) {
    log("USB enumeration control limit: %u bytes", unsigned(CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE));
    log("PHY: rtc usb_conf %08lX wrap %08lX usj %08lX gintsts %08lX", (unsigned long)REG_READ(RTC_CNTL_USB_CONF_REG),
        (unsigned long)REG_READ(USB_WRAP_OTG_CONF_REG), (unsigned long)REG_READ(USB_SERIAL_JTAG_CONF0_REG),
        (unsigned long)REG_READ(OTG_GINTSTS));
}

// Polling lets tasks leave the USB API before they delete themselves. Never
// delete another task: it may own an internal USB library mutex at that point.
static void lib_task(void *) {
    while (!lib_stop.load()) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(50), &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) all_free.store(true);
    }
    lib_running.store(false);
    vTaskDelete(nullptr);
}

static void client_cb(const usb_host_client_event_msg_t *m, void *) {
    if (m->event == USB_HOST_CLIENT_EVENT_NEW_DEV) new_addr.store(m->new_dev.address);
    else if (m->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        if (m->dev_gone.dev_hdl == claimed_device.load()) gone.store(true);
        // Preserve NEW_DEV if replacement enumeration races the old removal.

    }
}

static void client_task(void *) {
    while (!client_stop.load()) usb_host_client_handle_events(client, pdMS_TO_TICKS(50));
    client_running.store(false);
    vTaskDelete(nullptr);
}

static void xfer_cb(usb_transfer_t *t) {
    if (t == x_out) pending_out.store(false);
    else if (t == x_in) pending_in.store(false);
    else if (t == x_ctl) pending_ctl.store(false);
    xSemaphoreGive(done);
}

static bool pending() { return pending_out.load() || pending_in.load() || pending_ctl.load(); }

static bool await_false(const std::atomic<bool>& flag) {
    const TickType_t start = xTaskGetTickCount();
    while (flag.load()) {
        if (xTaskGetTickCount() - start >= pdMS_TO_TICKS(CLEANUP_MS)) return false;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return true;
}

static bool close_device() {
    if (!dev) return true;
    if (claimed) {
        const TickType_t start = xTaskGetTickCount();
        for (;;) {
            const esp_err_t result = usb_host_interface_release(client, dev, iface);
            if (result == ESP_OK) break;
            // DEV_GONE can leave a pending endpoint event briefly, even after
            // its final transfer callback. Let the client handler drain it.
            if (result != ESP_ERR_INVALID_STATE ||
                xTaskGetTickCount() - start >= pdMS_TO_TICKS(CLEANUP_MS)) return false;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        claimed = false;
    }
    if (usb_host_device_close(client, dev) != ESP_OK) return false;
    claimed_device.store(nullptr);
    dev = nullptr;
    dev_addr = ep_in = ep_out = iface = alternate = 0;
    midi_interface = false;
    mps_in = 64;
    gone.store(false);
    return true;
}

bool stop() {
    // This API belongs to the application worker, between complete commands.
    // A failed cleanup deliberately preserves resources still owned by USB.
    ready = false;
    if (cleanup_failed || executing.load()) { cleanup_failed = true; return false; }
    if (installed) {
        // Disconnect before freeing devices so enumeration cannot add another.
        // INVALID_STATE can mean the root port is already powered off.
        esp_err_t power = usb_host_lib_set_root_port_power(false);
        if (power != ESP_OK && power != ESP_ERR_INVALID_STATE) {
            cleanup_failed = true; return false;
        }
        const TickType_t start = xTaskGetTickCount();
        while (pending()) {
            // Device removal cancels bulk and EP0 transfers. Keep both event
            // tasks alive until every transfer callback has returned ownership.
            if (xTaskGetTickCount() - start >= pdMS_TO_TICKS(CLEANUP_MS)) {
                cleanup_failed = true; return false;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (!close_device()) { cleanup_failed = true; return false; }
    }
    if (client_running.load()) {
        client_stop.store(true);
        usb_host_client_unblock(client);
        if (!await_false(client_running)) { cleanup_failed = true; return false; }
    }
    if (client) {
        if (usb_host_client_deregister(client) != ESP_OK) { cleanup_failed = true; return false; }
        client = nullptr;
    }
    // No callback can now reference these allocations or their semaphore.
    for (usb_transfer_t** transfer : {&x_out, &x_in, &x_ctl}) {
        if (*transfer && usb_host_transfer_free(*transfer) != ESP_OK) {
            cleanup_failed = true; return false;
        }
        *transfer = nullptr;
    }
    if (done) { vSemaphoreDelete(done); done = nullptr; }
    if (installed) {
        all_free.store(false);
        esp_err_t freed = usb_host_device_free_all();
        if (freed != ESP_OK && freed != ESP_ERR_NOT_FINISHED) { cleanup_failed = true; return false; }
        if (freed == ESP_ERR_NOT_FINISHED) {
            const TickType_t start = xTaskGetTickCount();
            while (!all_free.load()) {
                if (xTaskGetTickCount() - start >= pdMS_TO_TICKS(CLEANUP_MS)) {
                    cleanup_failed = true; return false;
                }
                // Partial start may not have created a library task.
                if (!lib_running.load()) {
                    uint32_t flags = 0;
                    usb_host_lib_handle_events(pdMS_TO_TICKS(5), &flags);
                    if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) all_free.store(true);
                } else vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
        lib_stop.store(true);
        usb_host_lib_unblock();
        if (!await_false(lib_running)) { cleanup_failed = true; return false; }
        // Drain NO_CLIENTS or deferred free work even when free_all returned
        // immediately. Only this worker handles library events after joining.
        const TickType_t uninstallStart = xTaskGetTickCount();
        for (;;) {
            uint32_t flags = 0;
            usb_host_lib_handle_events(0, &flags);
            const esp_err_t result = usb_host_uninstall();
            if (result == ESP_OK) break;
            if (result != ESP_ERR_INVALID_STATE ||
                xTaskGetTickCount() - uninstallStart >= pdMS_TO_TICKS(CLEANUP_MS)) {
                cleanup_failed = true; return false;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        installed = false;
    }
    if (previous_log) { esp_log_set_vprintf(previous_log); previous_log = nullptr; }
    new_addr.store(-1);
    gone.store(false);
    io_fault.store(false);
    trace = nullptr;
    tag = 0;
    idf_len = 0;
    return true;
}

// Arduino's ESP-IDF has CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK set, and
// with that option ESP-IDF cancels every device unless a filter accepts it.
static bool enum_filter(const usb_device_desc_t *, uint8_t *bConfigurationValue) {
    *bConfigurationValue = 1;
    return true;
}

bool start() {
    if (cleanup_failed) return false; // An incomplete teardown requires an M5 restart.
    if (ready) return true;
    if (installed || client || done || x_out || x_in || x_ctl) {
        if (!stop()) return false;
    }
    new_addr.store(-1);
    gone.store(false);
    io_fault.store(false);
    lib_stop.store(false);
    client_stop.store(false);
    all_free.store(false);
    pending_out.store(false); pending_in.store(false); pending_ctl.store(false);
    tag = 0; dev_addr = ep_in = ep_out = iface = alternate = 0; mps_in = 64;
    midi_interface = false;
    claimed = false; trace = nullptr; idf_len = 0;
    previous_log = esp_log_set_vprintf(idf_vprintf);
    esp_log_level_set("*", ESP_LOG_ERROR);
    for (const char *name : {"ENUM", "HUB", "USBH", "USB HOST", "HCD DWC", "EXT_PORT", "EXT_HUB"})
        esp_log_level_set(name, ESP_LOG_DEBUG);
    usb_host_config_t cfg = {};
    cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.enum_filter_cb = enum_filter;
    if (usb_host_install(&cfg) != ESP_OK) { stop(); return false; }
    installed = true;
    lib_running.store(true);
    if (xTaskCreatePinnedToCore(lib_task, "usb_lib", 4096, nullptr, 6, nullptr, 0) != pdPASS) {
        lib_running.store(false); stop(); return false;
    }
    done = xSemaphoreCreateBinary();
    if (!done || usb_host_transfer_alloc(OUT_SIZE, 0, &x_out) != ESP_OK ||
        usb_host_transfer_alloc(IN_SIZE, 0, &x_in) != ESP_OK ||
        usb_host_transfer_alloc(64, 0, &x_ctl) != ESP_OK) { stop(); return false; }
    usb_host_client_config_t cc = {};
    cc.is_synchronous = false;
    cc.max_num_event_msg = 5;
    cc.async.client_event_callback = client_cb;
    if (usb_host_client_register(&cc, &client) != ESP_OK) { stop(); return false; }
    client_running.store(true);
    if (xTaskCreatePinnedToCore(client_task, "usb_client", 4096, nullptr, 5, nullptr, 0) != pdPASS) {
        client_running.store(false); stop(); return false;
    }
    ready = true;
    return true;
}

bool port_attached() { return ready && (REG_READ(OTG_HPRT) & 1); }

bool connected() { return ready && dev && claimed && !gone.load() && !io_fault.load(); }

// Walks the active configuration for the first interface with a bulk IN and
// a bulk OUT endpoint.
static bool find_bulk(const usb_config_desc_t *cd) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(cd);
    const int total = cd->wTotalLength;
    int current = -1;
    uint8_t in = 0, out = 0, alt = 0;
    uint16_t mps = 64;
    bool supported = false, midi = false;
    for (int i = 0; i + 1 < total && p[i] >= 2; i += p[i]) {
        if (i + p[i] > total) return false;
        if (p[i + 1] == 0x04 && p[i] >= 9) {
            current = p[i + 2]; alt = p[i + 3];
            midi = p[i + 5] == 1 && p[i + 6] == 3 && p[i + 7] == 0;
            const bool bot = p[i + 5] == 8 && p[i + 7] == 0x50;
            supported = midi || bot;
            in = out = 0;
        } else if (p[i + 1] == 0x05 && p[i] >= 7 && current >= 0 && supported && (p[i + 3] & 3) == 2) {
            const uint16_t packet = uint16_t(p[i + 4]) | (uint16_t(p[i + 5]) << 8);
            // The S3 host is full speed: legal bulk max packets are 8/16/32/64.
            // Reject descriptor values which could round an IN request beyond
            // its allocated buffer or use endpoint zero/reserved address bits.
            if (packet < 8 || packet > 64 || (packet & (packet - 1)) ||
                (p[i + 2] & 0x70) || !(p[i + 2] & 0x0f)) return false;
            if (p[i + 2] & 0x80) { in = p[i + 2]; mps = packet; }
            else out = p[i + 2];
            if (in && out) {
                iface = uint8_t(current); alternate = alt;
                ep_in = in; ep_out = out; mps_in = mps;
                midi_interface = midi;
                return true;
            }
        }
    }
    return false;
}

bool wait_device(uint32_t timeout_ms, uint16_t *vid, uint16_t *pid, Log log, char *err, size_t errlen) {
    if (!ready || cleanup_failed || dev || !vid || !pid || !log) {
        snprintf(err, errlen, "USB host is not ready for a new device");
        return false;
    }
    *vid = *pid = 0;
    gone.store(false);
    TickType_t t0 = xTaskGetTickCount();
    char last[96] = "", now[96], last_ch[160] = "", now_ch[160];
    int ch_logged = 0;
    TickType_t last_fn = t0;
    log_phy(log);
    while (new_addr < 0) {
        unsigned long ms = (xTaskGetTickCount() - t0) * portTICK_PERIOD_MS;
        port_state(now, sizeof now);
        if (strcmp(now, last) != 0) {
            log("%5lu ms %s", ms, now);
            strcpy(last, now);
        }
        channel_state(now_ch, sizeof now_ch);
        if (strcmp(now_ch, last_ch) != 0 && ch_logged < 40) {
            log("%5lu ms %s", ms, now_ch);
            strcpy(last_ch, now_ch);
            ch_logged++;
        }
        if (xTaskGetTickCount() - last_fn > pdMS_TO_TICKS(2000)) {
            last_fn = xTaskGetTickCount();
            log("%5lu ms frame %04lX gintsts %08lX haint %02lX", ms, (unsigned long)(REG_READ(0x60080408) & 0xFFFF),
                (unsigned long)REG_READ(OTG_GINTSTS), (unsigned long)REG_READ(0x60080414));
        }
        drain_idf_log(log);
        if (xTaskGetTickCount() - t0 > pdMS_TO_TICKS(timeout_ms)) {
            log_phy(log);
            snprintf(err, errlen, "%s (%lu ms). %.88s",
                     port_attached() ? "USB detected; enumeration timed out" : "No USB attachment; check data cable/power",
                     (unsigned long)timeout_ms, now);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    drain_idf_log(log);
    trace = log;
    const int address = new_addr.exchange(-1);
    if (usb_host_device_open(client, uint8_t(address), &dev) != ESP_OK) {
        snprintf(err, errlen, "could not open the USB device");
        return false;
    }
    claimed_device.store(dev);
    usb_device_info_t di;
    if (usb_host_device_info(dev, &di) != ESP_OK) {
        snprintf(err, errlen, "could not read USB device information");
        if (!close_device()) cleanup_failed = true;
        return false;
    }
    dev_addr = di.dev_addr;
    const usb_device_desc_t *dd;
    if (usb_host_get_device_descriptor(dev, &dd) != ESP_OK) {
        snprintf(err, errlen, "could not read the USB device descriptor");
        if (!close_device()) cleanup_failed = true;
        return false;
    }
    *vid = dd->idVendor;
    *pid = dd->idProduct;
    const usb_config_desc_t *cd;
    if (usb_host_get_active_config_descriptor(dev, &cd) != ESP_OK || !find_bulk(cd)) {
        snprintf(err, errlen, "%04X:%04X has no bulk interface", *vid, *pid);
        if (!close_device()) cleanup_failed = true;
        return false;
    }
    if (usb_host_interface_claim(client, dev, iface, alternate) != ESP_OK) {
        snprintf(err, errlen, "could not claim interface %u", iface);
        if (!close_device()) cleanup_failed = true;
        return false;
    }
    claimed = true;
    return true;
}

bool wait_reconnect(uint32_t timeout_ms, uint16_t *vid, uint16_t *pid, Log log, char *err, size_t errlen) {
    if (vid) *vid = 0;
    if (pid) *pid = 0;
    if (!ready || cleanup_failed || !dev || !vid || !pid || !log || pending() || executing.load()) {
        snprintf(err, errlen, "USB handoff is not ready");
        return false;
    }
    const TickType_t started = xTaskGetTickCount();
    const TickType_t limit = pdMS_TO_TICKS(timeout_ms);
    log("Waiting for USB device removal; host stays active");
    while (!gone.load()) {
        if (xTaskGetTickCount() - started >= limit) {
            snprintf(err, errlen, "FM-1 did not leave its previous USB mode");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (!close_device()) {
        cleanup_failed = true;
        snprintf(err, errlen, "Could not release old USB interface; restart M5");
        return false;
    }
    const TickType_t elapsed = xTaskGetTickCount() - started;
    if (elapsed >= limit) { snprintf(err, errlen, "USB handoff timed out"); return false; }
    log("Old USB interface released; waiting for replacement");
    return wait_device((limit - elapsed) * portTICK_PERIOD_MS, vid, pid, log, err, errlen);
}

// Runs one transfer and waits for it. A transfer that times out is halted,
// flushed (its callback then runs as canceled) and cleared.
static usb_transfer_status_t run(usb_transfer_t *t, uint8_t ep, int n, uint32_t timeout_ms = TIMEOUT_MS) {
    if (pending() || io_fault.load()) return USB_TRANSFER_STATUS_ERROR;
    t->device_handle = dev;
    t->bEndpointAddress = ep;
    t->num_bytes = n;
    t->callback = xfer_cb;
    t->context = nullptr;
    xSemaphoreTake(done, 0);
    auto& active = t == x_out ? pending_out : pending_in;
    active.store(true);
    if (usb_host_transfer_submit(t) != ESP_OK) { active.store(false); return USB_TRANSFER_STATUS_ERROR; }
    if (xSemaphoreTake(done, pdMS_TO_TICKS(timeout_ms) > 0 ? pdMS_TO_TICKS(timeout_ms) : 1) != pdTRUE) {
        usb_host_endpoint_halt(dev, ep);
        usb_host_endpoint_flush(dev, ep);
        if (xSemaphoreTake(done, pdMS_TO_TICKS(1000)) != pdTRUE || active.load()) {
            io_fault.store(true); // Preserve the in-flight allocation until stop().
            return USB_TRANSFER_STATUS_TIMED_OUT;
        }
        usb_host_endpoint_clear(dev, ep);
        return USB_TRANSFER_STATUS_TIMED_OUT;
    }
    return t->status;
}

static bool control(uint8_t type, uint8_t req, uint16_t value, uint16_t index) {
    if (pending() || io_fault.load()) return false;
    usb_setup_packet_t *s = (usb_setup_packet_t *)x_ctl->data_buffer;
    s->bmRequestType = type;
    s->bRequest = req;
    s->wValue = value;
    s->wIndex = index;
    s->wLength = 0;
    x_ctl->device_handle = dev;
    x_ctl->bEndpointAddress = 0;
    x_ctl->num_bytes = sizeof(usb_setup_packet_t);
    x_ctl->callback = xfer_cb;
    xSemaphoreTake(done, 0);
    pending_ctl.store(true);
    if (usb_host_transfer_submit_control(client, x_ctl) != ESP_OK) { pending_ctl.store(false); return false; }
    if (xSemaphoreTake(done, pdMS_TO_TICKS(2000)) != pdTRUE) { io_fault.store(true); return false; }
    return x_ctl->status == USB_TRANSFER_STATUS_COMPLETED;
}

// The data toggle of the host channel that serves ep, back to DATA0. A
// CLEAR_FEATURE(ENDPOINT_HALT) resets the device's toggle; ESP-IDF's
// usb_host_endpoint_clear() does not reset the host's (hcd_dwc.c
// _pipe_cmd_clear), which keeps it in HCTSIZ.PID (bits 29-30) between
// transfers. Out of step, each side drops the other's packets as repeats.
static void reset_toggle(uint8_t ep) {
    for (int ch = 0; ch < 8; ch++) {
        uint32_t base = 0x60080500 + ch * 0x20, c = REG_READ(base);
        bool match = ((c >> 11) & 0xF) == (ep & 0xF) && ((c >> 15) & 1) == (ep >> 7) &&
                     ((c >> 22) & 0x7F) == dev_addr && ((c >> 18) & 3) == 2;  // bulk
        if (match && !(c & 0x80000000)) REG_CLR_BIT(base + 0x10, 3u << 29);
    }
}

// CLEAR_FEATURE(ENDPOINT_HALT) on the device, then the host side of the pipe,
// its data toggle included.
static void clear_halt(uint8_t ep) {
    bool ok = control(0x02, 0x01, 0, ep);
    usb_host_endpoint_halt(dev, ep);
    usb_host_endpoint_flush(dev, ep);
    usb_host_endpoint_clear(dev, ep);
    reset_toggle(ep);
    if (trace) trace("USB: cleared halt on EP %02X%s", ep, ok ? "" : " (CLEAR_FEATURE failed)");
}

// Bulk-Only Mass Storage Reset, then both pipes cleared (BOT 5.3.4).
static void reset_recovery() {
    bool ok = control(0x21, 0xFF, 0, iface);
    if (trace) trace("USB: mass storage reset%s", ok ? "" : " failed");
    clear_halt(ep_in);
    clear_halt(ep_out);
}

static bool is_csw(const uint8_t *b, uint32_t *t) {
    uint32_t sig;
    memcpy(&sig, b, 4);
    if (sig != CSW_SIGNATURE) return false;
    memcpy(t, b + 4, 4);
    return true;
}

static Result transfer(const uint8_t *cdb, uint8_t cdb_len, uint8_t *buf, uint32_t len, bool in,
                       uint32_t *moved, char *err, size_t errlen) {
    if (!moved) { snprintf(err, errlen, "missing USB transfer count output"); return FAILED; }
    *moved = 0;
    if (!cdb || cdb_len < 1 || cdb_len > 16 || (len && !buf) ||
        (in ? len > IN_SIZE : len > OUT_SIZE)) {
        snprintf(err, errlen, "USB command exceeds transport bounds");
        return FAILED;
    }
    if (midi_interface) {
        snprintf(err, errlen, "USB MIDI interface does not accept ROM commands");
        return FAILED;
    }
    if (!connected()) {
        snprintf(err, errlen, "the unit is not connected");
        return FAILED;
    }
    tag++;
    uint8_t *c = x_out->data_buffer;
    memset(c, 0, 31);
    memcpy(c, &CBW_SIGNATURE, 4);
    memcpy(c + 4, &tag, 4);
    memcpy(c + 8, &len, 4);
    c[12] = in ? 0x80 : 0x00;
    c[13] = 0;  // LUN
    c[14] = cdb_len;
    memcpy(c + 15, cdb, cdb_len);
    if (run(x_out, ep_out, 31) != USB_TRANSFER_STATUS_COMPLETED) {
        snprintf(err, errlen, "cmd %02X%02X: sending it", cdb[0], cdb[1]);
        return FAILED;
    }

    uint8_t csw[13];
    bool early = false;
    if (len) {
        if (in) {
            int want = (len + mps_in - 1) / mps_in * mps_in;
            for (int skipped = 0;;) {
                usb_transfer_status_t st = run(x_in, ep_in, want);
                if (st == USB_TRANSFER_STATUS_STALL) {
                    if (trace) trace("USB: cmd %02X%02X data IN stalled", cdb[0], cdb[1]);
                    clear_halt(ep_in);  // stalled data phase: the status follows
                    break;
                }
                if (st != USB_TRANSFER_STATUS_COMPLETED) {
                    snprintf(err, errlen, "cmd %02X%02X: reading data (%d)", cdb[0], cdb[1], (int)st);
                    return FAILED;
                }
                uint32_t n = x_in->actual_num_bytes, t;
                // 13 bytes in place of more asked for may be a status block
                if (n == 13 && len > 13 && is_csw(x_in->data_buffer, &t)) {
                    if (t == tag) {
                        memcpy(csw, x_in->data_buffer, 13);
                        early = true;
                        break;
                    }
                    if (skipped++ < 2) continue;  // an earlier command's status, left over
                }
                *moved = n < len ? n : len;
                memcpy(buf, x_in->data_buffer, *moved);
                break;
            }
        } else {
            memcpy(x_out->data_buffer, buf, len);
            usb_transfer_status_t st = run(x_out, ep_out, len);
            if (st == USB_TRANSFER_STATUS_STALL) {
                if (trace) trace("USB: cmd %02X%02X data OUT stalled", cdb[0], cdb[1]);
                clear_halt(ep_out);
            } else if (st != USB_TRANSFER_STATUS_COMPLETED) {
                snprintf(err, errlen, "cmd %02X%02X: sending data (%d)", cdb[0], cdb[1], (int)st);
                return FAILED;
            } else {
                *moved = len;
            }
        }
    }
    if (!early) {
        usb_transfer_status_t st = run(x_in, ep_in, mps_in);
        if (st == USB_TRANSFER_STATUS_STALL) {
            if (trace) trace("USB: cmd %02X%02X status stalled", cdb[0], cdb[1]);
            clear_halt(ep_in);
            st = run(x_in, ep_in, mps_in);
        }
        if (st != USB_TRANSFER_STATUS_COMPLETED || x_in->actual_num_bytes != 13) {
            snprintf(err, errlen, "cmd %02X%02X: reading status (%d)", cdb[0], cdb[1], (int)st);
            return FAILED;
        }
        memcpy(csw, x_in->data_buffer, 13);
    }
    uint32_t t;
    if (!is_csw(csw, &t) || t != tag) {
        snprintf(err, errlen, "cmd %02X%02X: invalid status block", cdb[0], cdb[1]);
        return FAILED;
    }
    uint32_t residue;
    memcpy(&residue, csw + 8, 4);
    if (residue > len || (csw[12] == 0 && (*moved != len || residue != 0))) {
        snprintf(err, errlen, "USB short transfer or invalid residue");
        return FAILED;
    }
    if (csw[12] == 0) return OK;
    if (csw[12] == 1) {
        snprintf(err, errlen, "cmd %02X%02X refused (status 1)", cdb[0], cdb[1]);
        return REFUSED;
    }
    snprintf(err, errlen, "cmd %02X%02X: phase error (status %u)", cdb[0], cdb[1], csw[12]);
    return FAILED;
}

bool is_midi() { return connected() && midi_interface; }

bool midi_send_sysex(const uint8_t* bytes, size_t length, char* err, size_t errlen) {
    bool idle = false;
    if (!executing.compare_exchange_strong(idle, true)) {
        snprintf(err, errlen, "USB worker is already executing a command"); return false;
    }
    bool ok = false;
    if (!is_midi()) snprintf(err, errlen, "no USB MIDI interface connected");
    else {
        const size_t encoded = fm1::encodeUsbMidiSysEx(bytes, length, x_out->data_buffer, OUT_SIZE);
        if (!encoded) snprintf(err, errlen, "invalid or oversized MIDI SysEx request");
        else if (run(x_out, ep_out, int(encoded)) != USB_TRANSFER_STATUS_COMPLETED ||
                 x_out->actual_num_bytes != int(encoded))
            snprintf(err, errlen, "USB MIDI request transfer failed");
        else ok = true;
    }
    executing.store(false);
    return ok;
}

bool midi_receive_sysex(uint8_t* bytes, size_t capacity, uint32_t* length,
                        uint32_t timeout_ms, char* err, size_t errlen) {
    if (length) *length = 0;
    if (!bytes || !length || !capacity || !timeout_ms) {
        snprintf(err, errlen, "invalid MIDI reply buffer or timeout"); return false;
    }
    bool idle = false;
    if (!executing.compare_exchange_strong(idle, true)) {
        snprintf(err, errlen, "USB worker is already executing a command"); return false;
    }
    bool ok = false;
    fm1::UsbMidiSysExDecoder decoder;
    size_t wireLength = 0;
    const TickType_t start = xTaskGetTickCount();
    while (is_midi()) {
        const uint32_t elapsed = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
        if (elapsed >= timeout_ms) { snprintf(err, errlen, "USB MIDI reply timed out"); break; }
        const auto status = run(x_in, ep_in, mps_in, timeout_ms - elapsed);
        if (status != USB_TRANSFER_STATUS_COMPLETED) {
            snprintf(err, errlen, "USB MIDI reply transfer failed or timed out"); break;
        }
        const int received = x_in->actual_num_bytes;
        if (received < 0 || received > int(IN_SIZE) || received % 4) {
            snprintf(err, errlen, "malformed USB MIDI event packet length"); break;
        }
        bool malformed = false;
        for (int offset = 0; offset < received; offset += 4) {
            const auto result = decoder.feed(x_in->data_buffer + offset, bytes, capacity, wireLength);
            if (result == fm1::FeedResult::Malformed) {
                snprintf(err, errlen, "malformed or oversized MIDI SysEx reply"); malformed = true; break;
            }
            if (result == fm1::FeedResult::Complete) {
                *length = uint32_t(wireLength); ok = true; break;
            }
        }
        if (ok || malformed) break;
        vTaskDelay(1);
    }
    if (!ok && !is_midi()) snprintf(err, errlen, "USB MIDI disconnected during reply");
    executing.store(false);
    return ok;
}

Result execute(const uint8_t *cdb, uint8_t cdb_len, uint8_t *buf, uint32_t len, bool in,
               uint32_t *moved, char *err, size_t errlen) {
    bool idle = false;
    if (!executing.compare_exchange_strong(idle, true)) {
        if (moved) *moved = 0;
        snprintf(err, errlen, "USB worker is already executing a command");
        return FAILED;
    }
    Result r = transfer(cdb, cdb_len, buf, len, in, moved, err, errlen);
    if (r != OK && trace) trace("USB: %s: %s", r == REFUSED ? "refused" : "FAILED", err);
    // a refused command leaves the transport in order; a failed one is reset
    if (r == FAILED && connected() && !midi_interface) reset_recovery();
    executing.store(false);
    return r;
}

}  // namespace host
