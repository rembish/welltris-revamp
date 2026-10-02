/* Hall of fame in the original scores.bin format (10 x {name[16], int32 score, int32 lines}),
 * so the DOS file can be dropped in. */
#ifndef HOF_H
#define HOF_H

#include <stdint.h>

#define HOF_MAX  10
#define HOF_NAME 15

typedef struct {
    char name[HOF_NAME + 1];
    int32_t score, lines;
} hof_entry;

typedef struct {
    int count;
    hof_entry e[HOF_MAX];
} hof_table;

void hof_load(hof_table *t);
int hof_save(const hof_table *t);
/* the original lets any score in while the table is not full, else score >= the 10th */
int hof_qualifies(const hof_table *t, int32_t score);
/* inserts before the first entry with a lower or equal score; returns the row */
int hof_insert(hof_table *t, const char *name, int32_t score, int32_t lines);

#endif
