#include <string.h>

#include "obj_line.h"

/*
 * Software renderer, window unit (port-plan/render-design-obj.md section 5).
 * Works out the 6-bit enable mask (BG0-3, OBJ, effects) for each pixel of a
 * line. WIN0 beats WIN1, which beats the OBJ window, which beats outside.
 *
 * Edges follow mGBA 0.10.3 (video-software.c), the checkpoint oracle:
 *   - WINxH write: right = low byte, left = high byte. If left > 240 and
 *     left > right, left = 0. Then, if right > 240, right = 240 and a left
 *     above 240 becomes 240. (The left test runs first.)
 *   - _breakWindow: if right < left the window wraps: [0, right) and
 *     [left, 240). Otherwise it covers [left, right), right exclusive.
 *   - Vertical: inside if top <= line < bottom; when top > bottom, inside if
 *     line >= top or line < bottom.
 */

/* Horizontal spans of one window, after mGBA's clamp. The second span may be empty. */
static void WinSpanH(uint16_t winh, int *s0, int *e0, int *s1, int *e1)
{
    int right = winh & 0xFF;
    int left = winh >> 8;

    if (left > RENDER_WIDTH && left > right)
        left = 0;
    if (right > RENDER_WIDTH)
    {
        right = RENDER_WIDTH;
        if (left > RENDER_WIDTH)
            left = RENDER_WIDTH;
    }
    if (right < left)
    {
        *s0 = 0;
        *e0 = right;
        *s1 = left;
        *e1 = RENDER_WIDTH;
    }
    else
    {
        *s0 = left;
        *e0 = right;
        *s1 = 0;
        *e1 = 0;
    }
}

static int WinInsideV(uint16_t winv, int line)
{
    int bottom = winv & 0xFF;
    int top = winv >> 8;

    if (bottom >= top)
        return line >= top && line < bottom;
    return line >= top || line < bottom;
}

static void WinPaint(int line, int id, uint16_t winh, uint16_t winv, uint8_t bits, uint8_t *mask)
{
    int s0, e0, s1, e1;
    int left = winh >> 8;
    int right = winh & 0xFF;

    /* T6: legal values the port models; reported to learn whether FE8 uses them. */
    if (left > right || right > RENDER_WIDTH)
        RenderFxTrip(RENDER_TRIP_WINDOW, line, ((uint32_t)id << 16) | winh);

    if (!WinInsideV(winv, line))
        return;
    WinSpanH(winh, &s0, &e0, &s1, &e1);
    if (e0 > s0)
        memset(mask + s0, bits, (size_t)(e0 - s0));
    if (e1 > s1)
        memset(mask + s1, bits, (size_t)(e1 - s1));
}

void RenderWindowLine(int line, const struct RenderFxRegs *fx, const uint8_t *objWin, uint8_t *mask)
{
    uint16_t dispcnt = fx->dispcnt;
    int x;

    /* No window enabled: every layer and effects everywhere. */
    if (!(dispcnt & 0xE000))
    {
        memset(mask, RENDER_WIN_ALL, RENDER_WIDTH);
        return;
    }

    /* Paint back to front: outside, OBJ window, WIN1, WIN0. */
    memset(mask, fx->winout & RENDER_WIN_ALL, RENDER_WIDTH);

    /* The OBJ window needs DISPCNT bits 12 (OBJ on) and 15 (OBJ window on). */
    if ((dispcnt & 0x9000) == 0x9000 && objWin != NULL)
    {
        uint8_t bits = (uint8_t)((fx->winout >> 8) & RENDER_WIN_ALL);

        for (x = 0; x < RENDER_WIDTH; x++)
        {
            if (objWin[x])
                mask[x] = bits;
        }
    }

    if (dispcnt & 0x4000)
        WinPaint(line, 1, fx->win1h, fx->win1v, (uint8_t)((fx->winin >> 8) & RENDER_WIN_ALL), mask);
    if (dispcnt & 0x2000)
        WinPaint(line, 0, fx->win0h, fx->win0v, (uint8_t)(fx->winin & RENDER_WIN_ALL), mask);
}
