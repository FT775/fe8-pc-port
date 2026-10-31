// chapter title images

#pragma once

#include "global.h"
#include "chap_title_pointer.h"

typedef struct {
    char * save; // save image
    char * null_1; // useless, chap image in jp ver
    char * null_2; // useless, chap title in jp ver
} ChapTitle, * pChapTitle;

struct ChapterTitleFxSt {
    u16 chr_bg;
    u16 chr_str;
#ifdef PC_PORT
} AGBCC_STRUCT;
#else
};
#endif

extern struct ChapterTitleFxSt gChapterTitleFxSt;

extern ChapTitle chap_title_data[];
