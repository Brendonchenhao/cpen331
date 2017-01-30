/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>
#include <airballoon.h>

#define NROPES 16           // Number of ropes to be severed.
#define INVALID_IDX -1      // Special number denoting an invalid index.

// Array of ropes and corresponding locks.
struct rope *ropes[NROPES];
struct lock *rope_locks[NROPES];

// Array of stakes and corresponding locks.
struct stake *stakes[NROPES];
struct lock *stake_locks[NROPES];

// Working thread condition variable and lock.
struct cv *work_cv;
struct lock *work_lk;

// Main thread condition variable and lock.
struct cv *main_cv;
struct lock *main_lk;

// Counter and associated lock.
static int ropes_left = NROPES;
struct lock *counter_lock;

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 *
 * The ropes and stakes are represented by two arrays of pointers to their respective structs, as defined in
 * kern/include/airballoon.h. A rope is always attached to the same hook until severed, so their indices are the same.
 * There are also two arrays of locks with a one-to-one index mapping, so that individual elements can be accessed in a
 * thread-safe manner (more on this later). Accessing an index in the stakes array provides access to a linked list
 * of stake structs, allowing mappings of multiple hooks to a single stake. An empty stake is denoted by one whose
 * sk_next parameter points to null and sk_hook_idx is the value of INVALID_IDX (-1). When a rope is attached to a
 * stake, it is attached to the front of the linked list at that index.
 *
 * The main running thread (airballoon) does most of the maintenance work, setting up the data structures,
 * and starting each of the worker threads. It then waits on the main_cv condition variable until signalled by the
 * balloon thread. When it resumes, it cleans up the space we have allocated, and exits.
 *
 * The balloon thread simply waits for the other worker threads to complete. As we are waiting on Dandelion to
 * escape in the balloon, the balloon thread waits to be signalled by Dandelion before printing its victory message
 * and signalling the main thread.
 *
 * Dandelion, Marigold, and Flowerkiller are working according to their respective behaviours as described in the
 * assignment (Dandelion is special in that it signals the balloon thread when work is finished). Threads know to exit
 * when there are no ropes left, as tracked by the ropes_left counter. Dandelion and Marigold must decrement this
 * counter in a thread-safe manner, and so acquire, decrement, and subsequently release the counter lock whenever
 * handling this. These three worker threads exit when ropes_left reaches 0.
 *
 * In this implementation, worker threads first lock the rope structure, then the corresponding stake structure(s), then
 * the counter, with a nested release structure (released in reverse order). This order is maintained chiefly to avoid
 * deadlock.
 *
 */

/**
 * Creates a rope given the specified name.
 * @param name Name to be given to the rope struct.
 * @return Pointer to the newly created rope.
 */
struct rope *
rope_create(const char *name) {
    struct rope *rope;

    rope = kmalloc(sizeof(struct rope));
    if (rope == NULL) {
        return NULL;
    }

    rope->rp_name = kstrdup(name);
    if (rope->rp_name == NULL) {
        kfree(rope);
        return NULL;
    }

    return rope;
}

/**
 * Destroys the specified rope, freeing the memory allocated for it.
 * @param rope Rope to be destroyed.
 */
void
rope_destroy(struct rope *rope) {
    if (rope == NULL) {
        return;
    }

    kfree(rope->rp_name);
    kfree(rope);
}

/**
 * Creates a stake struct with the specified name.
 * @param name Name to identify the stake struct.
 * @return Pointer to the newly created stake.
 */
struct stake *
stake_create(const char *name) {
    struct stake *stake;

    stake = kmalloc(sizeof(struct stake));
    if (stake == NULL) {
        return NULL;
    }

    stake->sk_name = kstrdup(name);
    if (stake->sk_name == NULL) {
        kfree(stake);
        return NULL;
    }

    return stake;
}

/**
 * Destroys the specified stake structure, freeing the memory allocated for it.
 * @param stake Stake struct to be destroyed.
 */
void
stake_destroy(struct stake *stake) {
    if (stake == NULL) {
        return;
    }

    kfree(stake->sk_name);
    kfree(stake);
}

/**
 * Initializes and shuffles an array of integers of size NROPES.
 * @return Pointer to a random permutation of NROPES integers, 0 to NROPES-1
 */
static
int *
generate_mappings() {
    static int mappings[NROPES];

    // Generate a one-to-one mapping of indices to integers.
    for (int i = 0; i < NROPES; i++) {
        mappings[i] = i;
    }

    // Use Fisher-Yates to shuffle the values randomly.
    for (int i = NROPES - 1; i >= 0; i--) {
        int j = random() % (i + 1);

        int tmp = mappings[i];
        mappings[i] = mappings[j];
        mappings[j] = tmp;
    }

    return mappings;
}

/**
 * Initializes data structures, locks, and mappings required for this problem.
 */
static
void
set_up() {
    int *mappings = generate_mappings();

    for (int hook_idx = 0; hook_idx < NROPES; hook_idx++) {
        int stake_idx = mappings[hook_idx];     // Find stake index that maps to this hook index.

        // Set up rope struct at index of hook, and its lock.
        ropes[hook_idx] = rope_create("Rope");
        ropes[hook_idx]->rp_stake_idx = stake_idx;
        ropes[hook_idx]->is_attached = true;

        rope_locks[hook_idx] = lock_create("Rope Lock");

        // Set up struct representing an empty stake at this index.
        struct stake *empty_stake = stake_create("Empty Stake");
        empty_stake->sk_hook_idx = INVALID_IDX;
        empty_stake->sk_next = NULL;

        // Set up stake struct at index of stake, and its lock.
        stakes[stake_idx] = stake_create("Stake");
        stakes[stake_idx]->sk_hook_idx = hook_idx;
        stakes[stake_idx]->sk_next = empty_stake;

        stake_locks[stake_idx] = lock_create("Stake Lock");
    }

    // Set up counter lock.
    counter_lock = lock_create("Counter Lock");

    // Set up thread lock and CV.
    work_lk = lock_create("Working Thread Lock");
    work_cv = cv_create("Working Thread CV");

    // Set up main thread lock and CV.
    main_lk = lock_create("Main Thread Lock");
    main_cv = cv_create("Main Thread CV");

}

/**
 * Frees memory allocated that is left over after the test has run.
 */
static
void
tear_down() {

    // Iterate over our arrays, freeing everything.
    for (int i = 0; i < NROPES; i++) {
        rope_destroy(ropes[i]);         // Destroy rope structs and locks.
        lock_destroy(rope_locks[i]);

        stake_destroy(stakes[i]);       // Destroy stake structs and locks.
        lock_destroy(stake_locks[i]);
    }

    // Destroy counter lock.
    lock_destroy(counter_lock);

    // Destroy working thread lock and condition variable.
    cv_destroy(work_cv);
    lock_destroy(work_lk);

    // Destroy main thread lock and condition variable.
    cv_destroy(main_cv);
    lock_destroy(main_lk);
}

/**
 * Removes the stake struct stored at stake_idx which is mapped to hook_idx. If the stake mapped to hook_idx does not
 * exist at stake_idx, nothing is done.
 * @param stake_idx Stake index where we should look for the stake.
 * @param hook_idx Hook mapping of the stake we want to delete.
 */
static
void
remove_stake(int stake_idx, int hook_idx) {
    struct stake *cur_stk = stakes[stake_idx];

    // If the stake we want to remove is first at this index, point start to the next 'knot' at this stake,
    // and free the struct we want to remove.
    if (cur_stk->sk_hook_idx == hook_idx) {
        stakes[stake_idx] = cur_stk->sk_next;
        stake_destroy(cur_stk);

    } else {
        // Find the stake we want to remove.
        struct stake *prev_stk = cur_stk;
        while (cur_stk->sk_hook_idx != hook_idx && cur_stk->sk_hook_idx != INVALID_IDX) {
            prev_stk = cur_stk;
            cur_stk = cur_stk->sk_next;
        }

        // If the stake we want to remove exists, remove it from the linked list and free.
        if (cur_stk->sk_hook_idx != INVALID_IDX) {
            prev_stk->sk_next = cur_stk->sk_next;
            stake_destroy(cur_stk);
        }
    }
}

/**
 * Moves the first stake at old_stake_idx to the front of the linked list at new_stake_idx.
 * @param old_stake_idx Index from which to move a stake
 * @param new_stake_idx Index to which to move the stake
 */
static
void
move_stake(int old_stake_idx, int new_stake_idx) {
    struct stake *stake_to_move = stakes[old_stake_idx];

    // Move the stake struct from old_stake_idx to new_stake_idx, maintaining list order.
    stakes[old_stake_idx] = stake_to_move->sk_next;
    stake_to_move->sk_next = stakes[new_stake_idx];
    stakes[new_stake_idx] = stake_to_move;
}

/**
 * Prince Dandelion's run thread.
 * @param p
 * @param arg
 */
static
void
dandelion(void *p, unsigned long arg) {
    (void) p;
    (void) arg;

    lock_acquire(work_lk);  // We use Dandelion's thread to track progress. Acquire the working thread lock.
    kprintf("Dandelion thread starting\n");

    // Prince Dandelion is in the balloon, selecting hooks to detach.
    // Keep doing work until no ropes are left.
    while (ropes_left > 0) {

        int hook_idx = random() % NROPES;           // Pick a random rope.
        int stake_idx = ropes[hook_idx]->rp_stake_idx;

        lock_acquire(rope_locks[hook_idx]);         // Lock the rope first.
        lock_acquire(stake_locks[stake_idx]);       // Lock the stake attached to our rope.

        // Only sever rope if it is attached and the stake is mapped to our hook.
        if (ropes[hook_idx]->is_attached &&
            stake_idx == ropes[hook_idx]->rp_stake_idx) {

            ropes[hook_idx]->is_attached = false;

            remove_stake(stake_idx, hook_idx);

            // Safely decrement the counter.
            lock_acquire(counter_lock);
            ropes_left--;
            lock_release(counter_lock);

            kprintf("Dandelion severed rope %d\n", hook_idx);
        }

        lock_release(stake_locks[stake_idx]);
        lock_release(rope_locks[hook_idx]);

        thread_yield();     // Let another thread run.
    }


    kprintf("Dandelion thread done\n");
    cv_signal(work_cv, work_lk);    // We're done! Signal to balloon that we've severed all ropes,
    lock_release(work_lk);          // and release the working thread lock
}

/**
 * Princess Marigold's run thread.
 * @param p
 * @param arg
 */
static
void
marigold(void *p, unsigned long arg) {
    (void) p;
    (void) arg;

    kprintf("Marigold thread starting\n");

    // Marigold is on the ground, selecting stakes to detach.
    // Keep doing work until no ropes are left.
    while (ropes_left > 0) {

        int stake_idx = random() % NROPES;              // Pick a random stake.

        int hook_idx = stakes[stake_idx]->sk_hook_idx;  // Find the rope attached to this stake.

        if (hook_idx != INVALID_IDX) {                  // Only proceed if the stake has a rope attached to it.

            lock_acquire(rope_locks[hook_idx]);         // Lock the rope first.
            lock_acquire(stake_locks[stake_idx]);       // Lock the stake.

            // Only sever the rope if it is attached and the hook is mapped to our stake.
            if (ropes[hook_idx]->is_attached &&
                stake_idx == ropes[hook_idx]->rp_stake_idx) {

                ropes[hook_idx]->is_attached = false;

                remove_stake(stake_idx, hook_idx);

                // Safely decrement the counter.
                lock_acquire(counter_lock);
                ropes_left--;
                lock_release(counter_lock);

                kprintf("Marigold severed rope %d from stake %d\n", hook_idx, stake_idx);
            }

            lock_release(stake_locks[stake_idx]);
            lock_release(rope_locks[hook_idx]);
        }

        thread_yield();     // Let another thread run.
    }

    kprintf("Marigold thread done\n");
}

/**
 * Lord FlowerKiller's run thread.
 * @param p
 * @param arg
 */
static
void
flowerkiller(void *p, unsigned long arg) {
    (void) p;
    (void) arg;

    kprintf("Lord FlowerKiller thread starting\n");

    // Lord FlowerKiller is on the ground, randomly moving ropes between stakes.
    // Keep working until no ropes are left.
    while (ropes_left > 0) {

        int old_stake_idx = random() % NROPES;              // Pick a stake at random.
        int hook_idx = stakes[old_stake_idx]->sk_hook_idx;  // Find the rope attached to the stake.

        // Only continue if the stake is attached to a rope.
        if (hook_idx != INVALID_IDX) {

            lock_acquire(rope_locks[hook_idx]);             // Lock the rope first.
            lock_acquire(stake_locks[old_stake_idx]);       // Lock the stake.

            // Only sever the rope if it is attached and mapped to the stake we grabbed.
            if (ropes[hook_idx]->is_attached &&
                old_stake_idx == ropes[hook_idx]->rp_stake_idx) {

                int new_stake_idx = random() % NROPES;      // Randomly pick a stake to which to move the rope.

                // Do not move stake to the same stake as this would deadlock.
                if (old_stake_idx != new_stake_idx) {

                    lock_acquire(stake_locks[new_stake_idx]);

                    ropes[hook_idx]->rp_stake_idx = new_stake_idx;  // Move rope to new stake.
                    move_stake(old_stake_idx, new_stake_idx);

                    kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n",
                            hook_idx, old_stake_idx, new_stake_idx);

                    lock_release(stake_locks[new_stake_idx]);
                }
            }
            lock_release(stake_locks[old_stake_idx]);
            lock_release(rope_locks[hook_idx]);
        }

        thread_yield();     // Let another thread run.
    }

    kprintf("Lord FlowerKiller thread done\n");
}

/**
 * Balloon run thread.
 * @param p
 * @param arg
 */
static
void
balloon(void *p, unsigned long arg) {
    (void) p;
    (void) arg;

    kprintf("Balloon thread starting\n");
    lock_acquire(work_lk);      // Acquire main and working thread locks.
    lock_acquire(main_lk);

    // Wait for work to be done
    while (ropes_left > 0) {
        cv_wait(work_cv, work_lk);
    }
    lock_release(work_lk);

    kprintf("Balloon freed and Prince Dandelion escapes!\n");
    kprintf("Balloon thread done\n");

    // Dandelion has escaped! Signal main thread to resume.
    cv_signal(main_cv, main_lk);
    lock_release(main_lk);
}


/**
 * Main thread.
 * @param nargs
 * @param args
 * @return
 */
int
airballoon(int nargs, char **args) {

    int err = 0;

    (void) nargs;
    (void) args;
    (void) ropes_left;

    ropes_left = NROPES;    // Ensure that we have NROPES ropes on each run.

    set_up();               // Set up our data structures for this run.

    lock_acquire(main_lk);

    // Spin up our working threads.
    err = thread_fork("Marigold Thread",
                      NULL, marigold, NULL, 1);
    if (err)
        goto panic;

    err = thread_fork("Dandelion Thread",
                      NULL, dandelion, NULL, 2);
    if (err)
        goto panic;

    err = thread_fork("Lord FlowerKiller Thread",
                      NULL, flowerkiller, NULL, 3);
    if (err)
        goto panic;

    err = thread_fork("Air Balloon",
                      NULL, balloon, NULL, 0);
    if (err)
        goto panic;

    // Wait for working thread to finish.
    while (ropes_left > 0) {
        cv_wait(main_cv, main_lk);
    }
    lock_release(main_lk);

    goto done;
    panic:
    panic("airballoon: thread_fork failed: %s)\n",
          strerror(err));

    done:
    tear_down();    // Free all memory allocated for this run, and finish.
    kprintf("Main thread done\n");
    return 0;
}
