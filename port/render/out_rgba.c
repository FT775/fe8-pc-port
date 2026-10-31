#include <stdio.h>

#include "obj_line.h"

/*
 * Software renderer, output step (port-plan/render-design-obj.md section 9).
 *
 * D3: the one place that turns a BGR555 value into RGBA8888. Each 5-bit
 * channel v becomes (v << 3) | (v >> 2), the expansion mGBA uses
 * (mColorFrom555), so unblended pixels are byte-equal to mGBA's. The packed
 * u32 is R << 24 | G << 16 | B << 8 | A, the same on any host byte order.
 * The LUT below is only a cache of RenderBgr555ToRgba8888.
 *
 * The checkpoint hash is taken over the 15-bit frame, so D3 never changes a
 * test result.
 */

#define FRAME_PIXELS (RENDER_WIDTH * RENDER_HEIGHT)

static uint32_t sRgbaLut[0x8000];
static int sLutReady;

unsigned RenderExpand5(unsigned v)
{
    v &= 31;
    return (v << 3) | (v >> 2);
}

uint32_t RenderBgr555ToRgba8888(uint16_t c)
{
    uint32_t r = RenderExpand5(c);
    uint32_t g = RenderExpand5(c >> 5);
    uint32_t b = RenderExpand5(c >> 10);

    return (r << 24) | (g << 16) | (b << 8) | 0xFF;
}

void RenderOutInit(void)
{
    unsigned i;

    for (i = 0; i < 0x8000; i++)
        sRgbaLut[i] = RenderBgr555ToRgba8888((uint16_t)i);
    sLutReady = 1;
}

void RenderFrameToRgba8888(const uint16_t *src555, uint32_t *dst)
{
    int i;

    if (!sLutReady)
        RenderOutInit();
    for (i = 0; i < FRAME_PIXELS; i++)
        dst[i] = sRgbaLut[src555[i] & 0x7FFF];
}

/* FNV-1a 32 over the 38,400 u16 values as little-endian bytes (design 9.3). */
uint32_t RenderHashFrame555(const uint16_t *frame555)
{
    uint32_t h = 2166136261u;
    int i;

    for (i = 0; i < FRAME_PIXELS; i++)
    {
        h = (h ^ (frame555[i] & 0xFFu)) * 16777619u;
        h = (h ^ (uint32_t)(frame555[i] >> 8)) * 16777619u;
    }
    return h;
}

/* Binary PPM (P6) of one frame through D3. Returns 0 on success. */
int RenderDumpPpm555(const char *path, const uint16_t *frame555)
{
    FILE *f = fopen(path, "wb");
    int i;

    if (f == NULL)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", RENDER_WIDTH, RENDER_HEIGHT);
    for (i = 0; i < FRAME_PIXELS; i++)
    {
        uint32_t c = RenderBgr555ToRgba8888(frame555[i]);
        unsigned char rgb[3];

        rgb[0] = (unsigned char)(c >> 24);
        rgb[1] = (unsigned char)(c >> 16);
        rgb[2] = (unsigned char)(c >> 8);
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0 ? 0 : -1;
}
