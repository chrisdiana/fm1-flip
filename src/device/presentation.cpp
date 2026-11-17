#include "device/presentation.h"
#include <algorithm>
#include <sstream>

namespace device::presentation {
namespace {
constexpr uint32_t COLORS[] = {
    0x0b1218, 0x14212b, 0x173b42, 0x293d49, 0xf1f7f7, 0x92a8b5,
    0x63ddbd, 0x102d27, 0xffcd78, 0xff8e89, 0x85ddb0, 0x86bdff,
    0xbda6ef, 0x536b79, 0x000000, 0xffffff
};

uint64_t fingerprint(const Frame& frame) {
    uint64_t hash = 1469598103934665603ULL;
    auto add = [&](const std::string& value) {
        for (unsigned char c : value) { hash ^= c; hash *= 1099511628211ULL; }
        hash ^= 255; hash *= 1099511628211ULL;
    };
    add(std::to_string(static_cast<int>(frame.screen)));
    for (const auto* value : {&frame.title, &frame.eyebrow, &frame.right, &frame.subtitle,
                             &frame.heading, &frame.body, &frame.badge, &frame.footer,
                             &frame.editorValue, &frame.editorCharacter, &frame.editorGroup}) add(*value);
    for (const auto& row : frame.rows) {
        add(row.title); add(row.subtitle); add(row.badge); add(row.selected ? "1" : "0");
    }
    add(std::to_string(frame.completed)); add(std::to_string(frame.total));
    add(frame.busy ? "1" : "0"); add(frame.warning ? "1" : "0"); add(frame.connected ? "1" : "0");
    return hash;
}

void wrapped(Canvas& canvas, const std::string& value, int x, int y, int width, int lines,
             Color color = Color::Muted) {
    size_t begin = 0;
    for (int line = 0; line < lines && begin < value.size(); ++line) {
        size_t end = begin;
        while (end < value.size() && value[end] != '\n' &&
               canvas.textWidth(value.substr(begin, end - begin + 1)) <= width) ++end;
        if (end == begin) ++end;
        if (end < value.size() && value[end] != '\n') {
            size_t space = value.rfind(' ', end);
            if (space != std::string::npos && space > begin) end = space;
        }
        std::string text = value.substr(begin, end - begin);
        if (line == lines - 1 && end < value.size()) text = fit(text + "...", canvas, width);
        canvas.text(x, y + line * 11, text, color);
        begin = end;
        while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\n')) ++begin;
    }
}

void pill(Canvas& canvas, const std::string& value, int x, int y, Color foreground, Color background) {
    int width = canvas.textWidth(value) + 12;
    canvas.rect(x, y, width, 15, background, 5);
    canvas.text(x + 6, y + 4, value, foreground);
}

} // namespace

uint32_t rgb(Color color) { return COLORS[static_cast<unsigned>(color) & 15]; }

std::string fit(const std::string& value, const Canvas& canvas, int width, int scale) {
    if (canvas.textWidth(value, scale) <= width) return value;
    std::string result = value;
    while (!result.empty() && canvas.textWidth(result + "...", scale) > width) result.pop_back();
    return result + "...";
}

bool RepaintGate::needsPaint(const Frame& frame, uint32_t now) {
    uint64_t current = fingerprint(frame);
    if (painted_ && current == previous_) { deferred_ = false; return false; }
    if (painted_ && uint32_t(now - paintedAt_) < (frame.busy ? 125u : 16u)) { deferred_ = true; return false; }
    deferred_ = false;
    painted_ = true;
    paintedAt_ = now;
    previous_ = current;
    return true;
}

void render(const Frame& frame, Canvas& canvas) {
    const int width = canvas.width(), height = canvas.height(), footerY = height - 19;
    canvas.beginFrame(Color::Background);
    canvas.rect(0, 0, width, 3, frame.warning ? Color::Warning : Color::Accent);
    // Keep diagnostic titles readable: use normal size before truncating.
    const int titleScale = canvas.textWidth(frame.title, 2) <= width - 78 ? 2 : 1;
    canvas.text(10, 11, fit(frame.title, canvas, width - 78, titleScale), Color::Text, titleScale);
    if (!frame.right.empty()) {
        std::string right = fit(frame.right, canvas, 62);
        canvas.text(width - canvas.textWidth(right) - 9, 15, right,
                    frame.connected ? Color::Accent : Color::Muted);
    }
    if (!frame.eyebrow.empty()) canvas.text(11, 31, fit(frame.eyebrow, canvas, width - 22), Color::Muted);

    if (!frame.rows.empty()) {
        int y = 43;
        for (const auto& row : frame.rows) {
            if (y + 21 > footerY - 2) break;
            canvas.rect(7, y - 3, width - 14, 23, row.selected ? Color::Selected : Color::Surface, 5);
            if (row.selected) canvas.rect(7, y + 1, 3, 17, Color::Accent, 1);
            int badgeWidth = row.badge.empty() ? 0 : canvas.textWidth(row.badge) + 9;
            canvas.text(15, y + 1, fit(row.title, canvas, width - 36 - badgeWidth), Color::Text);
            canvas.text(15, y + 12, fit(row.subtitle, canvas, width - 35), Color::Muted);
            if (!row.badge.empty()) canvas.text(width - badgeWidth - 6, y + 1, row.badge, Color::Accent);
            y += 24;
        }
    } else if (frame.screen == Screen::Editor) {
        canvas.rect(9, 44, width - 18, 23, Color::Surface, 5);
        canvas.text(15, 52, fit(frame.editorValue + "_", canvas, width - 34), Color::Text);
        if (frame.editorGroup == "keyboard") {
            canvas.text(13, 80, "Type using the keyboard", Color::Accent);
            wrapped(canvas, frame.body, 13, 96, width - 26, 1);
        } else {
            canvas.rect(9, 73, 52, 34, Color::Selected, 6);
            canvas.text(17, 81, fit(frame.editorCharacter, canvas, 38, 2), Color::Accent, 2);
            canvas.text(72, 77, frame.editorGroup, Color::Text);
            wrapped(canvas, frame.body, 72, 91, width - 83, 2);
        }
    } else if (frame.screen == Screen::Progress) {
        canvas.text(12, 48, fit(frame.heading, canvas, width - 24), Color::Text);
        wrapped(canvas, frame.body, 12, 65, width - 24, 2);
        int barWidth = width - 24;
        canvas.rect(12, 94, barWidth, 6, Color::Line, 3);
        uint32_t progress = frame.total ? std::min<uint64_t>(barWidth, uint64_t(frame.completed) * barWidth / frame.total) : 0;
        if (progress) canvas.rect(12, 94, progress, 6, Color::Accent, 3);
        canvas.text(12, 105, frame.total ? std::to_string(uint64_t(frame.completed) * 100 / frame.total) + "%" : "Working...", Color::Muted);
    } else {
        int y = 46;
        if (!frame.badge.empty()) {
            pill(canvas, fit(frame.badge, canvas, width - 36), 11, y - 4,
                 frame.warning ? Color::Warning : Color::Accent, Color::Surface);
            y += 19;
        }
        if (!frame.heading.empty()) {
            canvas.text(11, y, fit(frame.heading, canvas, width - 22), Color::Text);
            y += 14;
        }
        if (!frame.subtitle.empty()) {
            canvas.text(11, y, fit(frame.subtitle, canvas, width - 22), Color::Muted);
            y += 13;
        }
        wrapped(canvas, frame.body, 11, y, width - 22, std::max(1, (footerY - y - 2) / 11));
    }

    canvas.rect(0, footerY, width, 19, Color::Surface);
    canvas.text(8, footerY + 6, fit(frame.footer, canvas, width - 16), Color::Muted);
    canvas.present();
}

bool renderIfChanged(const Frame& frame, Canvas& canvas, RepaintGate& gate, uint32_t now) {
    if (!gate.needsPaint(frame, now)) return false;
    render(frame, canvas);
    return true;
}

} // namespace device::presentation
