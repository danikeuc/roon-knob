#include "display_rotation_dial.h"

#include <stdint.h>

static bool valid_degrees(uint16_t degrees) {
    return degrees == 0 || degrees == 90 || degrees == 180 || degrees == 270;
}

bool dial_rotation_rect(uint16_t degrees, int screen_width, int screen_height,
                        dial_rect_t source, dial_rect_t *destination) {
    if (!destination || !valid_degrees(degrees) || screen_width <= 0 ||
        screen_height <= 0 || source.x < 0 || source.y < 0 ||
        source.width <= 0 || source.height <= 0 ||
        source.x >= screen_width || source.y >= screen_height ||
        source.width > screen_width - source.x ||
        source.height > screen_height - source.y) return false;

    dial_rect_t rotated;
    switch (degrees) {
    case 0: rotated = source; break;
    case 90: rotated = (dial_rect_t){screen_height - source.y - source.height,
                                    source.x, source.height, source.width}; break;
    case 180: rotated = (dial_rect_t){screen_width - source.x - source.width,
                                     screen_height - source.y - source.height,
                                     source.width, source.height}; break;
    default: rotated = (dial_rect_t){source.y,
                                     screen_width - source.x - source.width,
                                     source.height, source.width}; break;
    }
    *destination = rotated;
    return true;
}

bool dial_rotation_pixels(uint16_t degrees, const uint16_t *source,
                          size_t width, size_t height, uint16_t *destination,
                          size_t destination_pixels) {
    if (!valid_degrees(degrees) || !source || !destination ||
        width == 0 || height == 0 || width > SIZE_MAX / height ||
        width * height > SIZE_MAX / sizeof(uint16_t) ||
        destination_pixels < width * height) return false;

    /* Tiles bound source traversal while 90/270 output remains tightly packed. */
    const size_t tile = 16;
    for (size_t by = 0; by < height;) {
        size_t rows = height - by < tile ? height - by : tile;
        for (size_t bx = 0; bx < width;) {
            size_t columns = width - bx < tile ? width - bx : tile;
            for (size_t y = by; y < by + rows; ++y) {
                for (size_t x = bx; x < bx + columns; ++x) {
                    size_t target;
                    switch (degrees) {
                    case 0: target = y * width + x; break;
                    case 90: target = x * height + (height - 1 - y); break;
                    case 180: target = (height - 1 - y) * width + (width - 1 - x); break;
                    default: target = (width - 1 - x) * height + y; break;
                    }
                    destination[target] = source[y * width + x];
                }
            }
            bx += columns;
        }
        by += rows;
    }
    return true;
}

bool dial_rotation_touch(uint16_t degrees, int screen_width, int screen_height,
                         dial_point_t physical, dial_point_t *logical) {
    if (!logical || !valid_degrees(degrees) || screen_width <= 0 ||
        screen_height <= 0 || physical.x < 0 || physical.y < 0) return false;
    int physical_width = degrees == 90 || degrees == 270 ? screen_height : screen_width;
    int physical_height = degrees == 90 || degrees == 270 ? screen_width : screen_height;
    if (physical.x >= physical_width || physical.y >= physical_height) return false;

    dial_point_t point;
    switch (degrees) {
    case 0: point = physical; break;
    case 90: point = (dial_point_t){physical.y, screen_height - 1 - physical.x}; break;
    case 180: point = (dial_point_t){screen_width - 1 - physical.x,
                                     screen_height - 1 - physical.y}; break;
    default: point = (dial_point_t){screen_width - 1 - physical.y, physical.x}; break;
    }
    *logical = point;
    return true;
}
