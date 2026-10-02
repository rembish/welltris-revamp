#include "hof.h"
#include "store.h"
#include <string.h>

static int32_t rd32(const unsigned char *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }

static void wr32(unsigned char *p, int32_t v)
{
    uint32_t u = (uint32_t)v;
    for (int i = 0; i < 4; i++) p[i] = (unsigned char)(u >> (8 * i));
}

void hof_load(hof_table *t)
{
    unsigned char b[240];
    memset(t, 0, sizeof *t);
    if (store_read("scores.bin", b, sizeof b) != (int)sizeof b) return;
    for (int i = 0; i < HOF_MAX; i++) {
        const unsigned char *e = b + 24 * i;
        if (rd32(e + 16) == -1 && rd32(e + 20) == -1) break; /* end marker */
        memcpy(t->e[i].name, e, HOF_NAME);
        t->e[i].name[HOF_NAME] = 0;
        t->e[i].score = rd32(e + 16);
        t->e[i].lines = rd32(e + 20);
        t->count++;
    }
}

int hof_save(const hof_table *t)
{
    unsigned char b[240];
    memset(b, 0, sizeof b);
    for (int i = 0; i < HOF_MAX; i++) {
        unsigned char *e = b + 24 * i;
        if (i < t->count) {
            strncpy((char *)e, t->e[i].name, 16);
            wr32(e + 16, t->e[i].score);
            wr32(e + 20, t->e[i].lines);
        } else if (i == t->count) {
            wr32(e + 16, -1);
            wr32(e + 20, -1);
        }
    }
    return store_write("scores.bin", b, sizeof b) == (int)sizeof b;
}

int hof_qualifies(const hof_table *t, int32_t score)
{
    return t->count < HOF_MAX || (uint32_t)score >= (uint32_t)t->e[HOF_MAX - 1].score;
}

int hof_insert(hof_table *t, const char *name, int32_t score, int32_t lines) /* 21af */
{
    int i = 0;
    while (i < t->count && (uint32_t)t->e[i].score > (uint32_t)score) i++;
    if (i >= HOF_MAX) return -1;
    if (t->count < HOF_MAX) t->count++;
    memmove(&t->e[i + 1], &t->e[i], (size_t)(t->count - 1 - i) * sizeof t->e[0]);
    memset(&t->e[i], 0, sizeof t->e[i]);
    strncpy(t->e[i].name, name, HOF_NAME);
    t->e[i].score = score;
    t->e[i].lines = lines;
    return i;
}
