#include "device/local_controller.h"
#include <algorithm>
#include <cstring>

namespace device::local {
namespace {
constexpr const char* GROUPS[] = {
    "abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789",
    " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"
};
constexpr const char* LABELS[] = {"lowercase", "UPPERCASE", "numbers", "symbols", "edit actions"};
constexpr const char* ACTIONS[] = {"DEL", "CLR", "OK", "ESC"};
}

void TextEditor::begin(EditTarget target, const std::string& value, size_t limit) {
    target_ = target;
    passwordVisible_ = false;
    limit_ = std::min<size_t>(limit, 128);
    value_ = value.substr(0, limit_);
    if (target_ == EditTarget::Search) for (char& c : value_) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    value_.reserve(limit_);
    group_ = character_ = 0;
}

bool TextEditor::type(char character) {
    if (character < 32 || character > 126 || value_.size() >= limit_) return false;
    if (target_ == EditTarget::Search && character >= 'A' && character <= 'Z') character += 'a' - 'A';
    value_.push_back(character);
    return true;
}

void TextEditor::backspace() { if (!value_.empty()) value_.pop_back(); }

void TextEditor::nextCharacter() {
    character_ = (character_ + 1) % (group_ == 4 ? actionCount() : strlen(GROUPS[group_]));
}

void TextEditor::moveCharacter(int direction) {
    const size_t count = group_ == 4 ? actionCount() : strlen(GROUPS[group_]);
    if (direction < 0) {
        if (character_) --character_;
        else { do { group_ = (group_ + 4) % 5; } while (target_ == EditTarget::Search && group_ == 1); character_ = (group_ == 4 ? actionCount() : strlen(GROUPS[group_])) - 1; }
    } else if (++character_ >= count) { nextGroup(); }
}

void TextEditor::nextGroup() { do { group_ = (group_ + 1) % 5; } while (target_ == EditTarget::Search && group_ == 1); character_ = 0; }

EditorResult TextEditor::choose() {
    if (group_ != 4) type(GROUPS[group_][character_]);
    else if (character_ == 0) backspace();
    else if (character_ == 1) value_.clear();
    else if (character_ == 2) return EditorResult::Done;
    else if (character_ == 3) return EditorResult::Cancelled;
    else togglePasswordVisibility();
    return EditorResult::Edited;
}

std::string TextEditor::displayedValue() const {
    return target_ == EditTarget::Password && !passwordVisible_ ? std::string(value_.size(), '*') : value_;
}

std::string TextEditor::characterLabel() const {
    if (group_ == 4) return character_ == 4 ? (passwordVisible_ ? "HIDE" : "SHOW") : ACTIONS[character_];
    char value = GROUPS[group_][character_];
    return value == ' ' ? "SP" : std::string(1, value);
}

const char* TextEditor::groupLabel() const { return LABELS[group_]; }

} // namespace device::local
