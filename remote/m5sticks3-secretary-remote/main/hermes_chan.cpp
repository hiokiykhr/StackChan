#include "hermes_chan.h"

#include "M5Unified.h"
#include "hermes_chan_frames.generated.h"

namespace hermes_buddy {
namespace {

constexpr uint16_t COLOR_BUBBLE = 0xFF9E;
constexpr uint16_t COLOR_BUBBLE_EDGE = 0xD41F;
constexpr uint16_t COLOR_STROKE = 0x1082;
constexpr uint16_t COLOR_ACCENT = 0xFEA0;
constexpr uint16_t COLOR_ALERT = 0xFD20;
constexpr uint16_t COLOR_ERROR = 0xF986;
constexpr uint16_t COLOR_HAPPY = 0x18C3;

frames::Expression expression_for_mood(HermesChanMood mood)
{
    switch (mood) {
    case HermesChanMood::Busy:
    case HermesChanMood::Attention:
        return frames::Expression::Surprised;
    case HermesChanMood::Error:
        return frames::Expression::Angry;
    case HermesChanMood::Sleep:
        return frames::Expression::Happy;
    case HermesChanMood::Idle:
    case HermesChanMood::Celebrate:
    case HermesChanMood::Heart:
    default:
        return frames::Expression::Happy;
    }
}

uint16_t accent_color_for_mood(HermesChanMood mood)
{
    switch (mood) {
    case HermesChanMood::Attention:
        return COLOR_ALERT;
    case HermesChanMood::Error:
        return COLOR_ERROR;
    case HermesChanMood::Celebrate:
    case HermesChanMood::Heart:
        return COLOR_ACCENT;
    case HermesChanMood::Busy:
        return 0x97F2;
    case HermesChanMood::Sleep:
        return 0x7BEF;
    case HermesChanMood::Idle:
    default:
        return COLOR_HAPPY;
    }
}

size_t frame_index_for_mood(HermesChanMood mood, uint32_t tick)
{
    (void)tick;
    switch (mood) {
    case HermesChanMood::Sleep:
        return 7;
    case HermesChanMood::Busy:
    case HermesChanMood::Attention:
        return 2;
    case HermesChanMood::Error:
        return 3;
    case HermesChanMood::Celebrate:
    case HermesChanMood::Heart:
        return 4;
    case HermesChanMood::Idle:
    default:
        return 0;
    }
}

void draw_mask_frame(int x, int y, const frames::Frame &frame, uint16_t color)
{
    const int bytes_per_row = (frame.width + 7) / 8;
    for (int row = 0; row < frame.height; ++row) {
        const uint8_t *row_ptr = frame.data + (row * bytes_per_row);
        int run_start = -1;
        for (int col = 0; col < frame.width; ++col) {
            const uint8_t byte = row_ptr[col / 8];
            const bool on = ((byte >> (7 - (col % 8))) & 0x01) != 0;
            if (on) {
                if (run_start < 0) {
                    run_start = col;
                }
            } else if (run_start >= 0) {
                M5.Display.drawFastHLine(x + run_start, y + row, col - run_start, color);
                run_start = -1;
            }
        }
        if (run_start >= 0) {
            M5.Display.drawFastHLine(x + run_start, y + row, frame.width - run_start, color);
        }
    }
}

void draw_emotion_marks(int cx, int cy, HermesChanMood mood, uint16_t color)
{
    if (mood == HermesChanMood::Attention || mood == HermesChanMood::Busy) {
        M5.Display.drawLine(cx - 44, cy - 46, cx - 38, cy - 54, color);
        M5.Display.drawLine(cx - 34, cy - 50, cx - 34, cy - 60, color);
        M5.Display.drawLine(cx - 24, cy - 46, cx - 18, cy - 54, color);
    }
    if (mood == HermesChanMood::Heart || mood == HermesChanMood::Celebrate) {
        M5.Display.fillCircle(cx + 30, cy - 46, 3, color);
        M5.Display.fillCircle(cx + 36, cy - 46, 3, color);
        M5.Display.fillTriangle(cx + 26, cy - 44, cx + 40, cy - 44, cx + 33, cy - 32, color);
    }
    if (mood == HermesChanMood::Error) {
        M5.Display.drawCircle(cx + 34, cy - 46, 5, color);
        M5.Display.drawLine(cx + 30, cy - 50, cx + 38, cy - 42, color);
        M5.Display.drawLine(cx + 30, cy - 42, cx + 38, cy - 50, color);
    }
}

}  // namespace

void draw_hermes_chan(int cx, int cy, HermesChanMood mood, uint32_t tick, bool compact)
{
    const auto expression = expression_for_mood(mood);
    const auto *set = frames::get_frames(expression);
    const auto &frame = set[frame_index_for_mood(mood, tick)];

    const int bubble_w = compact ? 70 : 92;
    const int bubble_h = compact ? 70 : 92;
    const int bubble_x = cx - bubble_w / 2;
    const int bubble_y = cy - bubble_h / 2;
    const uint16_t accent = accent_color_for_mood(mood);

    M5.Display.fillRoundRect(bubble_x, bubble_y, bubble_w, bubble_h, 18, COLOR_BUBBLE);
    M5.Display.drawRoundRect(bubble_x, bubble_y, bubble_w, bubble_h, 18, COLOR_BUBBLE_EDGE);
    M5.Display.drawRoundRect(bubble_x + 2, bubble_y + 2, bubble_w - 4, bubble_h - 4, 16, accent);

    const int draw_x = cx - static_cast<int>(frame.width) / 2;
    const int draw_y = cy - static_cast<int>(frame.height) / 2 + 4;
    draw_mask_frame(draw_x, draw_y, frame, COLOR_STROKE);
    draw_emotion_marks(cx, cy, mood, accent);
}

}  // namespace hermes_buddy
