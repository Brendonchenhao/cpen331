
#ifndef AIRBALLOON_H
#define AIRBALLOON_H

#include <types.h>
#include <lib.h>
#include <thread.h>

/*
 * Representation of a rope.
 *
 * rp_name: Name string used to identify this rope.
 * rp_stake_idx: Index of stake that this rope is mapped to.
 * is_attached: Current status of rope. True if rope is mapped to a stake.
 */
struct rope {
    char *rp_name;
    int rp_stake_idx;
    bool is_attached;
};

/*
 * Operations:
 *
 *      rope_create: Allocate memory for this rope. Do not initialize fields.
 *      rope_destroy: Free memory associated with this rope.
 */
struct rope *rope_create(const char *name);

void rope_destroy(struct rope *);

/*
 * Representation of a stake.
 *
 * sk_name: Name string used to identify this stake.
 * sk_next: Pointer to next stake (or 'knot') under this stake.
 * sk_hook_idx: Index of rope that this stake is mapped to.
 */
struct stake {
    char *sk_name;
    struct stake *sk_next;
    int sk_hook_idx;
};

/*
 * Operations:
 *
 *      stake_create: Allocate memory for this stake. Do not initialize fields.
 *      stake_destroy: Free memory associated with this stake.
 */
struct stake *stake_create(const char *name);

void stake_destroy(struct stake *);

/*
 * airballoon
 *
 *      main run thread function
 */
int airballoon(int nargs, char **args);

#endif /* AIRBALLOON_H */
