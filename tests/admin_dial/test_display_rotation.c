#include "display_rotation_dial.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

static void check_pixels(uint16_t degrees, const uint16_t *source,
                         size_t width, size_t height, const uint16_t *expected) {
    uint16_t destination[8] = {0};
    destination[width * height] = 0xCAFE;
    CHECK(dial_rotation_pixels(degrees, source, width, height,
                               destination, width * height));
    CHECK(memcmp(destination, expected, width * height * sizeof(uint16_t)) == 0);
    CHECK(destination[width * height] == 0xCAFE);
}

static void test_rotation_non_square_rgb565(void) {
    const uint16_t source[] = {1, 2, 3, 4, 5, 6};
    const uint16_t zero[] = {1, 2, 3, 4, 5, 6};
    const uint16_t ninety[] = {4, 1, 5, 2, 6, 3};
    const uint16_t half[] = {6, 5, 4, 3, 2, 1};
    const uint16_t two_seventy[] = {3, 6, 2, 5, 1, 4};
    check_pixels(0, source, 3, 2, zero);
    check_pixels(90, source, 3, 2, ninety);
    check_pixels(180, source, 3, 2, half);
    check_pixels(270, source, 3, 2, two_seventy);
    const uint16_t column[] = {7, 8, 9};
    const uint16_t reverse[] = {9, 8, 7};
    check_pixels(0, column, 1, 3, column);
    check_pixels(90, column, 1, 3, reverse);
    check_pixels(180, column, 1, 3, reverse);
    check_pixels(270, column, 1, 3, column);
    check_pixels(90, column, 3, 1, column);
    check_pixels(270, column, 3, 1, reverse);
}

static void check_rect(uint16_t degrees, int screen_width, int screen_height,
                       dial_rect_t source, dial_rect_t expected) {
    dial_rect_t actual = {-1, -1, -1, -1};
    CHECK(dial_rotation_rect(degrees, screen_width, screen_height, source, &actual));
    CHECK(actual.x == expected.x && actual.y == expected.y);
    CHECK(actual.width == expected.width && actual.height == expected.height);
}

static void test_rotation_rect_at_all_edges(void) {
    const dial_rect_t partial = {2, 1, 3, 2};
    check_rect(0, 8, 6, partial, (dial_rect_t){2, 1, 3, 2});
    check_rect(90, 8, 6, partial, (dial_rect_t){3, 2, 2, 3});
    check_rect(180, 8, 6, partial, (dial_rect_t){3, 3, 3, 2});
    check_rect(270, 8, 6, partial, (dial_rect_t){1, 3, 2, 3});
    const dial_rect_t corners[] = {{0,0,1,1},{7,0,1,1},{0,5,1,1},{7,5,1,1}};
    const dial_point_t expected90[] = {{5,0},{5,7},{0,0},{0,7}};
    for (size_t i = 0; i < 4; ++i) {
        check_rect(90, 8, 6, corners[i],
                   (dial_rect_t){expected90[i].x,expected90[i].y,1,1});
    }
    check_rect(90, 8, 6, (dial_rect_t){0,0,8,6}, (dial_rect_t){0,0,6,8});
    check_rect(270, 8, 6, (dial_rect_t){0,0,8,6}, (dial_rect_t){0,0,6,8});
}

static void test_touch_inverse_matches_pixels(void) {
    const uint16_t rotations[] = {0, 90, 180, 270};
    for (size_t r = 0; r < 4; ++r) {
        for (int y = 0; y < 6; y += 5) {
            for (int x = 0; x < 8; x += 7) {
                dial_rect_t rotated;
                dial_point_t logical = {-1,-1};
                CHECK(dial_rotation_rect(rotations[r], 8, 6,
                                         (dial_rect_t){x,y,1,1}, &rotated));
                CHECK(dial_rotation_touch(rotations[r], 8, 6,
                    (dial_point_t){rotated.x,rotated.y}, &logical));
                CHECK(logical.x == x && logical.y == y);
            }
        }
    }
    dial_point_t logical;
    CHECK(dial_rotation_touch(90, 8, 6, (dial_point_t){3,2}, &logical));
    CHECK(logical.x == 2 && logical.y == 2);
    CHECK(dial_rotation_touch(270, 8, 6, (dial_point_t){1,3}, &logical));
    CHECK(logical.x == 4 && logical.y == 1);
}

static void test_rotation_rejects_small_buffer(void) {
    const uint16_t source[] = {1,2,3,4,5,6};
    uint16_t destination[7] = {11,11,11,11,11,11,0xCAFE};
    CHECK(!dial_rotation_pixels(90, source, 3, 2, destination, 5));
    for (int i = 0; i < 6; ++i) CHECK(destination[i] == 11);
    CHECK(destination[6] == 0xCAFE);
    CHECK(!dial_rotation_pixels(45, source, 3, 2, destination, 6));
    CHECK(!dial_rotation_pixels(90, source, 0, 2, destination, 6));
    CHECK(!dial_rotation_pixels(90, source, SIZE_MAX, 2, destination, 6));
    CHECK(!dial_rotation_pixels(90, NULL, 3, 2, destination, 6));
    CHECK(!dial_rotation_pixels(90, source, 3, 2, NULL, 6));
    dial_rect_t rect = {42,42,42,42};
    CHECK(!dial_rotation_rect(45, 8, 6, (dial_rect_t){0,0,1,1}, &rect));
    CHECK(!dial_rotation_rect(90, 8, 6, (dial_rect_t){7,5,2,1}, &rect));
    CHECK(!dial_rotation_rect(90, 8, 6, (dial_rect_t){INT_MAX,0,2,1}, &rect));
    CHECK(!dial_rotation_rect(90, 0, 6, (dial_rect_t){0,0,1,1}, &rect));
    CHECK(rect.x == 42 && rect.y == 42);
    dial_point_t point = {42,42};
    CHECK(!dial_rotation_touch(90, 8, 6, (dial_point_t){6,0}, &point));
    CHECK(!dial_rotation_touch(270, 8, 6, (dial_point_t){0,8}, &point));
    CHECK(!dial_rotation_touch(90, 8, 6, (dial_point_t){-1,0}, &point));
    CHECK(!dial_rotation_touch(45, 8, 6, (dial_point_t){0,0}, &point));
    CHECK(point.x == 42 && point.y == 42);
}

static void test_tiled_rotation_and_boundaries(void) {
    enum { WIDTH = 19, HEIGHT = 17, PIXELS = WIDTH * HEIGHT };
    uint16_t source[PIXELS];
    uint16_t destination[PIXELS + 1];
    for (size_t i = 0; i < PIXELS; ++i) source[i] = (uint16_t)(i + 1);
    const uint16_t angles[] = {0, 90, 180, 270};
    for (size_t r = 0; r < 4; ++r) {
        memset(destination, 0, sizeof(destination));
        destination[PIXELS] = 0xCAFE;
        CHECK(dial_rotation_pixels(angles[r], source, WIDTH, HEIGHT,
                                   destination, PIXELS));
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                dial_rect_t physical;
                dial_point_t logical;
                CHECK(dial_rotation_rect(angles[r], WIDTH, HEIGHT,
                         (dial_rect_t){x,y,1,1}, &physical));
                CHECK(dial_rotation_touch(angles[r], WIDTH, HEIGHT,
                         (dial_point_t){physical.x,physical.y}, &logical));
                CHECK(logical.x == x && logical.y == y);
                int output_width = (angles[r] == 90 || angles[r] == 270) ? HEIGHT : WIDTH;
                CHECK(destination[physical.y * output_width + physical.x] ==
                      source[y * WIDTH + x]);
            }
        }
        CHECK(destination[PIXELS] == 0xCAFE);
    }
    const dial_rect_t edges[] = {
        {0,2,3,2}, {16,2,3,2}, {4,0,3,2}, {4,15,3,2}
    };
    for (size_t e = 0; e < 4; ++e) {
        for (size_t r = 0; r < 4; ++r) {
            dial_rect_t actual;
            CHECK(dial_rotation_rect(angles[r], WIDTH, HEIGHT, edges[e], &actual));
            CHECK(actual.width == ((r & 1) ? edges[e].height : edges[e].width));
            CHECK(actual.height == ((r & 1) ? edges[e].width : edges[e].height));
            CHECK(actual.x >= 0 && actual.y >= 0);
        }
    }
}

int main(void) {
    test_rotation_non_square_rgb565();
    test_rotation_rect_at_all_edges();
    test_touch_inverse_matches_pixels();
    test_rotation_rejects_small_buffer();
    test_tiled_rotation_and_boundaries();
    puts("display rotation: PASS");
}
