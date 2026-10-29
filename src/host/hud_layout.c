/* User-requested instrument arrangement for the USA 3.5 HUD. Update only the
 * transient pixel caches consumed by the native widget constructors/reflow.
 * Fonts, unit labels, colors, visibility, and serialized layouts stay native. */
#include "recomp_types.h"
#include "hud_layout.h"

#define HUD_BASE_LIST 0x0060C818u
#define HUD_DELTA_LIST 0x0060C904u
#define HUD_INDEX 0x0060C95Cu
#define HUD_WIDTH 0x0060C900u
#define HUD_HEIGHT 0x0060C81Cu
#define DIGITAL_SPEED_X 0x0060C830u
#define DIGITAL_SPEED_Y 0x0060C834u
#define ANALOG_SPEED_X 0x0060C860u
#define ANALOG_SPEED_Y 0x0060C864u
#define DIGITAL_RPM_X 0x0060C888u
#define DIGITAL_RPM_Y 0x0060C88Cu

static uint32_t selected_record(uint32_t list, uint32_t index) {
    uint32_t object = MEM32(list), items;
    if (!object || !(items = MEM32(object))) return 0;
    return MEM32(items + index * 4);
}

static int overlaps(int x, int y, int width, int height,
                    int dial_x, int dial_y, int dial_width, int dial_height) {
    return x < dial_x + dial_width && x + width > dial_x &&
           y < dial_y + dial_height && y + height > dial_y;
}

static int clamp(int value, int maximum) {
    return value < 0 ? 0 : value > maximum ? maximum : value;
}

void nfspu_hud_layout_apply(void) {
    uint32_t index = MEM32(HUD_INDEX), base, delta;
    int width = (int)MEM32(HUD_WIDTH), height = (int)MEM32(HUD_HEIGHT);
    int speed_width, speed_height, rpm_width, rpm_height;
    int dial_x, dial_y, dial_width, dial_height, speed_y, rpm_y;
    if (index >= 64 || width < 640 || height < 480 || width > 8192 || height > 8192)
        return;
    base = selected_record(HUD_BASE_LIST, index);
    delta = selected_record(HUD_DELTA_LIST, index);
    if (!base || !delta || MEM32(delta + 0x30) != 2) return;

    /* 00420800 returns digital-speed size from base+9C/A0; 00420880
     * returns digital-RPM size from delta+14/18. 00420920 returns analog
     * speedometer size at base+D4/D8, and 00420BE0 its cached position. */
    speed_width = (int)MEM32(base + 0x9C);
    speed_height = (int)MEM32(base + 0xA0);
    rpm_width = (int)MEM32(delta + 0x14);
    rpm_height = (int)MEM32(delta + 0x18);
    dial_width = (int)MEM32(base + 0xD4);
    dial_height = (int)MEM32(base + 0xD8);
    dial_x = (int)MEM32(ANALOG_SPEED_X);
    dial_y = (int)MEM32(ANALOG_SPEED_Y);
    if (speed_width <= 0 || speed_width > width || speed_height <= 0 ||
        speed_height > height || rpm_width <= 0 || rpm_width > width ||
        rpm_height <= 0 || rpm_height > height || dial_width <= 0 ||
        dial_width > width || dial_height <= 0 || dial_height > height ||
        dial_x < 0 || dial_x >= width || dial_y < 0 || dial_y >= height)
        return;

    /* For the native 35-pixel rows these are H-85 and H-43. Keep an 8-pixel
     * bottom margin and 7 pixels between rows if a custom font changes size. */
    rpm_y = clamp(height - rpm_height - 8, height - rpm_height);
    speed_y = clamp(rpm_y - speed_height - 7, height - speed_height);
    if (overlaps((int)MEM32(DIGITAL_SPEED_X), (int)MEM32(DIGITAL_SPEED_Y),
                 speed_width, speed_height, dial_x, dial_y, dial_width, dial_height)) {
        MEM32(DIGITAL_SPEED_X) = (uint32_t)clamp(dial_x - speed_width - 12, width - speed_width);
        MEM32(DIGITAL_SPEED_Y) = (uint32_t)speed_y;
    }
    /* Mode 2 intentionally displays analog plus digital. Preserve a user's
     * standalone digital RPM placement (mode 1) and custom positions that
     * are already outside the analog cluster. */
    if (MEM32(delta + 0x38) == 2 &&
        overlaps((int)MEM32(DIGITAL_RPM_X), (int)MEM32(DIGITAL_RPM_Y),
                 rpm_width, rpm_height, dial_x, dial_y, dial_width, dial_height)) {
        MEM32(DIGITAL_RPM_X) = (uint32_t)clamp(dial_x - rpm_width - 12, width - rpm_width);
        MEM32(DIGITAL_RPM_Y) = (uint32_t)rpm_y;
    }
}
