#ifndef DISPLAY_ROTATION_DIAL_H
#define DISPLAY_ROTATION_DIAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { int x; int y; } dial_point_t;
typedef struct { int x; int y; int width; int height; } dial_rect_t;

/* A rectangle in logical screen coordinates maps to its physical display bounds. */
bool dial_rotation_rect(uint16_t degrees, int screen_width, int screen_height,
                        dial_rect_t source, dial_rect_t *destination);
/* Source and destination must not overlap; output is tightly packed RGB565.
 * Pixel values are copied without byte swapping. */
bool dial_rotation_pixels(uint16_t degrees, const uint16_t *source,
                          size_t width, size_t height, uint16_t *destination,
                          size_t destination_pixels);
/* Convert a physical display coordinate back to logical screen space. */
bool dial_rotation_touch(uint16_t degrees, int screen_width, int screen_height,
                         dial_point_t physical, dial_point_t *logical);

#endif
