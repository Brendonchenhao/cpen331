
#ifndef AIRBALLOON_H
#define AIRBALLOON_H

#include <types.h>
#include <lib.h>
#include <thread.h>

struct rope {
    char *rp_name;
    int rp_stake_idx;
    bool is_attached;
};

struct rope *rope_create(const char *name);

void rope_destroy(struct rope *);

struct stake {
    char *sk_name;
    struct stake *sk_next;
    int sk_hook_idx;
};

struct stake *stake_create(const char *name);

void stake_destroy(struct stake *);

int airballoon(int nargs, char **args);

#endif /* AIRBALLOON_H */
