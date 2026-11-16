#pragma once
#include "device/presentation.h"

namespace device::presentation {

// Buffer must be an off-screen sprite. No physical-display reference is retained,
// making a visible clear impossible while laying out or painting the next frame.
template<class Buffer>
class BufferedCanvas final : public Canvas {
public:
    BufferedCanvas(Buffer& buffer, bool indexed) : buffer_(buffer), indexed_(indexed) {}
    void setIndexed(bool indexed) { indexed_ = indexed; }
    int width() const override { return buffer_.width(); }
    int height() const override { return buffer_.height(); }
    void beginFrame(Color color) override {
        if (indexed_) buffer_.fillScreen(static_cast<uint8_t>(color));
        else buffer_.fillScreen(rgb(color));
    }
    void rect(int x, int y, int w, int h, Color color, int radius = 0) override {
        if (indexed_) buffer_.fillRoundRect(x, y, w, h, radius, static_cast<uint8_t>(color));
        else buffer_.fillRoundRect(x, y, w, h, radius, rgb(color));
    }
    void text(int x, int y, const std::string& value, Color color, int scale = 1) override {
        buffer_.setTextSize(scale);
        if (indexed_) buffer_.setTextColor(static_cast<uint8_t>(color));
        else buffer_.setTextColor(rgb(color));
        buffer_.drawString(value.c_str(), x, y);
    }
    int textWidth(const std::string& value, int scale = 1) const override {
        buffer_.setTextSize(scale);
        return buffer_.textWidth(value.c_str());
    }
    void present() override { buffer_.pushSprite(0, 0); }
private:
    Buffer& buffer_;
    bool indexed_;
};

} // namespace device::presentation
