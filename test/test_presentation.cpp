#include "device/presentation.h"
#include "device/display_canvas.h"
#include "device/local_controller.h"
#include <cassert>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
using namespace device::presentation;

struct PhysicalDisplay { int visibleClears = 0, presents = 0; };
struct OffscreenBuffer {
    PhysicalDisplay& physical;
    int clears = 0, scale = 1;
    std::vector<std::string> texts;
    int width() const { return 240; }
    int height() const { return 135; }
    void fillScreen(uint32_t) { ++clears; }
    void fillRoundRect(int, int, int, int, int, uint32_t) {}
    void setTextSize(int value) { scale = value; }
    void setTextColor(uint32_t) {}
    void drawString(const char* text, int, int) { texts.emplace_back(text); }
    int textWidth(const char* value) const { return std::char_traits<char>::length(value) * 6 * scale; }
    void pushSprite(int, int) { ++physical.presents; }
};

std::string escaped(const std::string& value) {
    std::string out;
    for (char c : value) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

struct SvgCanvas : Canvas {
    std::ofstream output;
    explicit SvgCanvas(const std::string& path) : output(path) {}
    int width() const override { return 240; }
    int height() const override { return 135; }
    std::string color(Color value) const {
        std::ostringstream out; out << '#' << std::hex << (0x1000000 | rgb(value));
        return "#" + out.str().substr(2);
    }
    void beginFrame(Color value) override {
        output << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 240 135\" width=\"960\" height=\"540\">";
        rect(0, 0, 240, 135, value);
    }
    void rect(int x, int y, int w, int h, Color value, int radius = 0) override {
        output << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << w << "\" height=\"" << h << "\" rx=\"" << radius << "\" fill=\"" << color(value) << "\"/>";
    }
    void text(int x, int y, const std::string& value, Color fill, int scale = 1) override {
        if (value.empty()) return;
        output << "<text x=\"" << x << "\" y=\"" << y + 7 * scale << "\" font-family=\"monospace\" font-size=\"" << 8 * scale << "\" textLength=\"" << textWidth(value, scale) << "\" lengthAdjust=\"spacingAndGlyphs\" fill=\"" << color(fill) << "\">" << escaped(value) << "</text>";
    }
    int textWidth(const std::string& value, int scale = 1) const override { return value.size() * 6 * scale; }
    void present() override { output << "</svg>"; }
};

void preview(const Frame& frame, const std::string& directory, const char* name) {
    if (directory.empty()) return;
    std::filesystem::create_directories(directory);
    SvgCanvas canvas(directory + "/" + name + ".svg");
    render(frame, canvas);
}

int main(int argc, char** argv) {
    using device::local::StickButtons;
    using device::local::StickAction;
    StickButtons buttons;
    assert(buttons.update(true, false, 0) == StickAction::None);
    assert(buttons.update(false, false, 100) == StickAction::Choose);
    assert(buttons.update(false, true, 200) == StickAction::None);
    assert(buttons.update(false, false, 300) == StickAction::Next);
    assert(buttons.update(true, false, 400) == StickAction::None);
    assert(buttons.update(true, false, 900) == StickAction::Back);
    assert(buttons.update(false, false, 1000) == StickAction::None);
    assert(buttons.update(false, true, 1100) == StickAction::None);
    assert(buttons.update(false, true, 1600) == StickAction::Previous);
    assert(buttons.update(false, false, 1700) == StickAction::None);
    // Staggered chord/release must not choose, move, hold or confirm.
    for (bool firstA : {false, true}) {
        assert(buttons.update(firstA, !firstA, 1800) == StickAction::None);
        assert(buttons.update(true, true, 1850) == StickAction::Actions);
        assert(buttons.update(true, true, 2400) == StickAction::None);
        assert(buttons.update(!firstA, firstA, 2450) == StickAction::None);
        assert(!buttons.confirmHeld(false, true));
        assert(buttons.update(false, false, 2500) == StickAction::None);
    }
    StickButtons repeat;
    assert(repeat.update(false, true, 0, 150) == StickAction::None);
    assert(repeat.update(false, true, 499, 150) == StickAction::None);
    assert(repeat.update(false, true, 500, 150) == StickAction::Previous);
    assert(repeat.update(false, true, 649, 150) == StickAction::None);
    assert(repeat.update(false, true, 650, 150) == StickAction::Previous);
    assert(repeat.update(false, true, 800, 150) == StickAction::Previous);
    assert(repeat.update(false, false, 850, 150) == StickAction::None);
    assert(repeat.update(false, false, 1100, 150) == StickAction::None);
    // Re-press restores the initial delay; a chord cancels repetition until release.
    assert(repeat.update(false, true, 1200, 150) == StickAction::None);
    assert(repeat.update(false, true, 1699, 150) == StickAction::None);
    assert(repeat.update(false, true, 1700, 150) == StickAction::Previous);
    assert(repeat.update(true, true, 1800, 150) == StickAction::Actions);
    assert(repeat.update(false, true, 2200, 150) == StickAction::None);
    assert(repeat.update(false, false, 2300, 150) == StickAction::None);
    assert(repeat.update(true, false, 2400, 150) == StickAction::None);
    assert(repeat.update(true, false, 2900, 150) == StickAction::Back);
    assert(repeat.update(true, false, 3500, 150) == StickAction::None);
    StickButtons rollover;
    assert(rollover.update(false, true, UINT32_MAX - 400) == StickAction::None);
    assert(rollover.update(false, true, 99) == StickAction::Previous);
    assert(rollover.update(false, true, 399) == StickAction::Previous);
    device::local::TextEditor traversal;
    traversal.begin(device::local::EditTarget::Search, "", 128);
    for (int i = 0; i < 73; ++i) traversal.moveCharacter(1);
    assert(traversal.characterLabel() == "a");
    traversal.moveCharacter(-1);
    assert(traversal.characterLabel() == "ESC");
    traversal.moveCharacter(1);
    assert(traversal.characterLabel() == "a");
    StickButtons slow;
    assert(slow.update(false,true,0)==StickAction::None);
    assert(slow.update(false,true,500)==StickAction::Previous);
    assert(slow.update(false,true,650)==StickAction::None);
    assert(slow.update(false,true,799)==StickAction::None);
    assert(slow.update(false,true,800)==StickAction::Previous);
    device::local::TextEditor search;
    search.begin(device::local::EditTarget::Search, "HeLLo", 48);
    assert(search.value()=="hello");
    assert(search.type('W')&&search.value()=="hellow");
    for(int i=0;i<73;++i){
        const auto label=search.characterLabel();
        assert(label.size()!=1 || label[0]<'A' || label[0]>'Z');
        search.moveCharacter(1);
    }
    assert(search.characterLabel()=="a");
    search.moveCharacter(-1);assert(search.characterLabel()=="ESC");
    search.nextGroup();assert(search.characterLabel()=="a");
    search.nextGroup();assert(std::string(search.groupLabel())=="numbers");
    const std::string directory = argc > 1 ? argv[1] : "";
    PhysicalDisplay display;
    OffscreenBuffer buffer{display, 0, 1, {}};
    BufferedCanvas<OffscreenBuffer> canvas(buffer, true);
    Frame wifiStatus;wifiStatus.title="Home";
    RepaintGate wifiGate;
    assert(renderIfChanged(wifiStatus,canvas,wifiGate,1));
    wifiStatus.wifiConnected=true;
    assert(renderIfChanged(wifiStatus,canvas,wifiGate,20));
    assert(!renderIfChanged(wifiStatus,canvas,wifiGate,40));
    Frame errorTitle;
    errorTitle.screen = Screen::Info;
    errorTitle.title = "Could not finish";
    errorTitle.body = "No USB device: port conn1 ena1 pwr1 line D+1 D-1 spd1";
    render(errorTitle, canvas);
    assert(std::find(buffer.texts.begin(), buffer.texts.end(), "Could not finish") != buffer.texts.end());
    display.presents = 0;
    buffer.clears = 0;
    Frame browse;
    browse.title = "Discover"; browse.eyebrow = "POPULAR  /  217 FIRMWARES";
    browse.right = "USB ROM"; browse.connected = true;
    browse.rows = {{"Hortator", "Cube · v1.2", "SAVED", true}, {"Bubba Machine", "Manatee · v2.1", "BETA", false}, {"Choralroot", "Scattered · v1.4", "", false}};
    browse.footer = "A Choose B Next Hold A Back";
    // Probe 1 isolates buffering: even repeated rendering never clears the panel.
    render(browse, canvas); render(browse, canvas); render(browse, canvas);
    assert(display.visibleClears == 0 && display.presents == 3 && buffer.clears == 3);
    // Probe 2 isolates scheduling: unchanged idle state paints once, however long idle lasts.
    display.presents = 0;
    RepaintGate gate;
    for (uint32_t now : {0u, 1100u, 2200u, 86400000u}) renderIfChanged(browse, canvas, gate, now);
    assert(display.presents == 1 && display.visibleClears == 0);
    browse.rows[0].selected = false; browse.rows[1].selected = true;
    assert(renderIfChanged(browse, canvas, gate, 86400100u));
    assert(display.presents == 2);
    preview(browse, directory, "discover");

    Frame progress;
    progress.screen = Screen::Progress; progress.title = "Installing";
    progress.eyebrow = "FM-1  /  KEEP USB CONNECTED"; progress.heading = "Verifying flash";
    progress.body = "Checking each written sector against the package.";
    progress.busy = true; progress.total = 100; progress.completed = 35;
    progress.footer = "Writing started - do not disconnect";
    RepaintGate progressGate;
    assert(renderIfChanged(progress, canvas, progressGate, 1000));
    progress.completed = 36;
    assert(!renderIfChanged(progress, canvas, progressGate, 1050));
    assert(renderIfChanged(progress, canvas, progressGate, 1125));
    assert(!renderIfChanged(progress, canvas, progressGate, 10000));
    preview(progress, directory, "progress");

    Frame editor;
    editor.screen = Screen::Editor; editor.title = "Wi-Fi key"; editor.eyebrow = "STUDIO NETWORK"; editor.right = "12/63";
    editor.editorValue = "************"; editor.editorCharacter = "a"; editor.editorGroup = "lowercase";
    editor.body = "Hold B: previous; choose OK to save"; editor.footer = "A Add B Next Hold A Back";
    preview(editor, directory, "password");
    editor.editorGroup = "keyboard"; editor.body = "Backspace deletes. Fn+` cancels.";
    editor.footer = "Type  Backspace Delete  Enter Save";
    preview(editor, directory, "password-cardputer");
    Frame menu = browse; menu.title = "FM-1 Flip"; menu.eyebrow = "HORTATOR  /  v1.2";
    menu.rows = {{"Download package", "Save for offline use", "", false}, {"Install on FM-1", "Backup created before writes", "", true}, {"Favorite", "Pin to your collection", "", false}};
    menu.footer = "A Choose B Next Hold A Back";
    preview(menu, directory, "actions");
    Frame confirm;
    confirm.screen = Screen::Confirm; confirm.title = "Install?"; confirm.eyebrow = "REVIEW BEFORE WRITING";
    confirm.badge = "PHYSICAL CONFIRMATION"; confirm.warning = true;
    confirm.heading = "Hortator 1.2"; confirm.body = "A fresh backup is required. Hold B for two seconds to begin.";
    confirm.footer = "Hold B Confirm  Hold A Cancel";
    preview(confirm, directory, "confirm");
    Frame wifi = browse; wifi.title = "Wi-Fi"; wifi.right = "3 FOUND"; wifi.eyebrow = "CHOOSE A NETWORK";
    wifi.rows = {{"Studio Network", "Password required", "-42", true}, {"Guest", "Open network", "-71", false}, {"Enter SSID manually", "For hidden networks", "", false}};
    preview(wifi, directory, "networks");
    Frame backups = browse; backups.title = "Backups"; backups.right = "2 FILES"; backups.eyebrow = "STORED ON THIS DEVICE";
    backups.rows = {{"Backup 4", "FM-1 · 1.0 MB", "KEEP", true}, {"Backup 3", "FM-1 · 1.0 MB", "", false}};
    preview(backups, directory, "backups");
    Frame loaded;
    loaded.screen = Screen::Loaded; loaded.title = "Loaded"; loaded.right = "LIVE"; loaded.connected = true;
    loaded.eyebrow = "IDENTIFIED ON CONNECTED FM-1"; loaded.badge = "USB IDENTITY VERIFIED";
    loaded.heading = "Hortator 1.2"; loaded.body = "This is the firmware reported by the FM-1, separate from downloaded packages.";
    loaded.footer = "A+B Home  Hold A Back";
    preview(loaded, directory, "loaded");

    device::local::TextEditor input;
    input.begin(device::local::EditTarget::Password, "", 128);
    for (char c = 32; c <= 126; ++c) assert(input.type(c));
    assert(input.value().size() == 95 && input.displayedValue() == std::string(95, '*'));
    input.togglePasswordVisibility();
    assert(input.displayedValue()==input.value());
    input.togglePasswordVisibility();
    assert(input.displayedValue()==std::string(95,'*'));
    input.begin(device::local::EditTarget::Password,"aB!",63);
    input.moveCharacter(-1);assert(input.characterLabel()=="SHOW");
    input.choose();assert(input.characterLabel()=="HIDE"&&input.displayedValue()=="aB!");
    input.choose();assert(input.displayedValue()=="***"&&input.value()=="aB!");
    input.togglePasswordVisibility();input.begin(device::local::EditTarget::Password,"aB!",63);
    assert(input.displayedValue()=="***");
    device::local::TextEditor picker;
    picker.begin(device::local::EditTarget::Password, "", 128);
    for (int group = 0; group < 4; ++group) {
        std::string first = picker.characterLabel();
        do { picker.choose(); picker.nextCharacter(); } while (picker.characterLabel() != first);
        picker.nextGroup();
    }
    std::string chosen = picker.value(), printable;
    std::sort(chosen.begin(), chosen.end());
    for (char c = 32; c <= 126; ++c) printable.push_back(c);
    assert(chosen == printable); // Every printable ASCII character is reachable with two buttons.
    picker.choose(); assert(picker.value().size() == 94); // DEL
    picker.nextCharacter(); picker.choose(); assert(picker.value().empty()); // CLR
    picker.nextCharacter(); assert(picker.choose() == device::local::EditorResult::Done);
    picker.nextCharacter(); assert(picker.choose() == device::local::EditorResult::Cancelled);
    input.begin(device::local::EditTarget::Password, "", 3);
    assert(input.type('a') && input.type('B') && input.type('!') && !input.type('x'));
    input.backspace(); assert(input.value() == "aB");
    for (int i = 0; i < 5; ++i) input.nextGroup();
    assert(std::string(input.groupLabel()) == "lowercase");
    std::cout << "visible full-screen clears=0; idle presents=1; buffered, changed-only and throttled progress passed\n";
    std::cout << "Full ASCII, password masking and bounded editor tests passed\n";
}
