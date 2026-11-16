#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace device::presentation {

enum class Color : uint8_t {
    Background, Surface, Selected, Line, Text, Muted, Accent, AccentText,
    Warning, Danger, Success, Blue, Purple, Dim, Black, White
};
uint32_t rgb(Color color);

enum class Screen { Browse, Detail, Menu, Editor, Networks, Backups, Setup, Confirm, Progress, Info, Loaded };
struct Row {
    std::string title, subtitle, badge;
    bool selected = false;
};
struct Frame {
    Screen screen = Screen::Browse;
    std::string title, eyebrow, right, subtitle, heading, body, badge, footer;
    std::string editorValue, editorCharacter, editorGroup;
    std::vector<Row> rows;
    uint32_t completed = 0, total = 0;
    bool busy = false, warning = false, connected = false;
};

class Canvas {
public:
    virtual ~Canvas() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    // beginFrame clears only the backing surface. present is the sole visible write.
    virtual void beginFrame(Color color) = 0;
    virtual void rect(int x, int y, int width, int height, Color color, int radius = 0) = 0;
    virtual void text(int x, int y, const std::string& text, Color color, int scale = 1) = 0;
    virtual int textWidth(const std::string& text, int scale = 1) const = 0;
    virtual void present() = 0;
};

class RepaintGate {
public:
    bool needsPaint(const Frame& frame, uint32_t now);
    void invalidate() { painted_ = false; }
    bool deferred() const { return deferred_; }
private:
    bool painted_ = false, deferred_ = false;
    uint64_t previous_ = 0;
    uint32_t paintedAt_ = 0;
};

void render(const Frame& frame, Canvas& canvas);
bool renderIfChanged(const Frame& frame, Canvas& canvas, RepaintGate& gate, uint32_t now);
std::string fit(const std::string& text, const Canvas& canvas, int width, int scale = 1);

} // namespace device::presentation
