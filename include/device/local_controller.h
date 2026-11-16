#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace device::local {

enum class StickAction { None, Choose, Next, Back, Previous, Actions };

// Consume an entire chord until both buttons release; never leak a tap or hold.
class StickButtons {
public:
    StickAction update(bool a, bool b, uint32_t now, uint32_t repeatMs = 300) {
        if (a && b) {
            const bool first = !chord_;
            chord_ = true;
            return first ? StickAction::Actions : StickAction::None;
        }
        if (chord_) {
            if (!a && !b) { chord_ = false; active_ = 0; held_ = false; }
            return StickAction::None;
        }
        const int pressed = a ? 1 : b ? 2 : 0;
        if (pressed != active_) {
            const int previous = active_;
            const bool wasHeld = held_;
            active_ = pressed; since_ = now; held_ = false;
            if (previous && !wasHeld) return previous == 1 ? StickAction::Choose : StickAction::Next;
        }
        if (pressed && !held_ && uint32_t(now - since_) >= 500) {
            held_ = true;
            repeatedAt_ = now;
            return pressed == 1 ? StickAction::Back : StickAction::Previous;
        }
        // Repeat only upward navigation. Back and Actions remain single-shot.
        if (pressed == 2 && held_ && uint32_t(now - repeatedAt_) >= repeatMs) {
            repeatedAt_ = now;
            return StickAction::Previous;
        }
        return StickAction::None;
    }
    bool confirmHeld(bool a, bool b) const { return b && !a && !chord_; }
private:
    int active_ = 0;
    uint32_t since_ = 0, repeatedAt_ = 0;
    bool held_ = false, chord_ = false;
};

enum class EditTarget { Search, Ssid, Password };
enum class EditorResult { Edited, Done, Cancelled };

// The same bounded editor serves the two-button character picker and keyboard.
class TextEditor {
public:
    void begin(EditTarget target, const std::string& value, size_t limit);
    bool type(char character);
    void backspace();
    void nextCharacter();
    void moveCharacter(int direction);
    void nextGroup();
    EditorResult choose();
    const std::string& value() const { return value_; }
    EditTarget target() const { return target_; }
    std::string displayedValue() const;
    std::string characterLabel() const;
    const char* groupLabel() const;
    size_t limit() const { return limit_; }
private:
    EditTarget target_ = EditTarget::Search;
    std::string value_;
    size_t limit_ = 48, group_ = 0, character_ = 0;
};

} // namespace device::local
