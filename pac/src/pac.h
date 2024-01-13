#ifndef PAC_PAC_H
#define PAC_PAC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "z80/z80.h"
#include "wsg.h"

/* CONSTANT DEFINITIONS */
#define PAC_CLOCK_SPEED 3072000L // 3.072 MHz (= number of cycles per second)
#define PAC_FPS 60
#define PAC_CYCLES_PER_FRAME (PAC_CLOCK_SPEED / PAC_FPS)
#define PAC_SCREEN_WIDTH 224
#define PAC_SCREEN_HEIGHT 288
// sprites and tiles are images that are stored in sprite/tile rom.
// in memory, those images are represented using vertical "strips"
// of 8*4px, each strip being 8 bytes long (each pixel is stored on two
// bits)
#define LEN_STRIP_BYTES (8)
// tiles are 8*8px images. in memory, they are composed of two strips.
#define TILE_WIDTH (8)
#define NB_PIXELS_PER_TILE (TILE_WIDTH * TILE_WIDTH)
#define NB_TILES (256)
// sprites are 16*16px images. in memory, they are composed of 8 strips.
#define SPRITE_WIDTH (16)
#define NB_PIXELS_PER_SPRITE (SPRITE_WIDTH * SPRITE_WIDTH)
#define NB_SPRITES (64)

/* MACROS */
#define UNUSED(param)         \
    do {                      \
        if (param == param) { \
        }                     \
    } while (0)

typedef struct pac pac;
struct pac {
    z80 cpu;
    uint8_t rom[0x10000];     // 0x0000-0x4000
    uint8_t ram[0x1000];      // 0x4000-0x5000
    uint8_t sprite_pos[0x10]; // 0x5060-0x506f

    uint8_t color_rom[32];
    uint8_t palette_rom[0x100];
    uint8_t tile_rom[0x1000];
    uint8_t sprite_rom[0x1000];
    uint8_t sound_rom1[0x100];
    uint8_t sound_rom2[0x100];

    uint8_t tiles[NB_TILES * NB_PIXELS_PER_TILE];       // to store predecoded tiles
    uint8_t sprites[NB_SPRITES * NB_PIXELS_PER_SPRITE]; // to store predecoded sprites

    uint8_t int_vector;
    bool vblank_enabled;
    bool sound_enabled;
    bool flip_screen;
    uint32_t speed;

    // in 0 port
    bool p1_up, p1_left, p1_right, p1_down, rack_advance, coin_s1, coin_s2, credits_btn;

    // in 1 port
    bool board_test, p1_start, p2_start;

    // ppu
    uint8_t screen_buffer[PAC_SCREEN_HEIGHT * PAC_SCREEN_WIDTH * 3];
    void (*update_screen)(pac *const n);

    // audio
    wsg sound_chip;
    int audio_buffer_len;
    int16_t *audio_buffer;
    int sample_rate;
    bool mute_audio;
    void (*push_sample)(int16_t);
};

void pac_quit(void);
int pac_init(pac *const p, const char *rom_dir);
void pac_update(pac *const p, uint32_t ms);
void pac_cheat_invincibility(pac *const p);

#ifdef __cplusplus
}
#endif

#endif // PAC_PAC_H
