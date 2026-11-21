#define _POSIX_C_SOURCE 200809L

/* Regression tests for shared parallel-job ceilings and for how a build
 * decides how many jobs it may use (#320).  Nothing here depends on the CPUs of
 * the host that runs it: the cgroup walk takes its mount point and its idea of
 * "this process's cgroup" as arguments, so a temporary tree stands in for both. */
#include "cbs.h"
#include "temp.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "jobs-test: %d: %s\n", __LINE__, #condition);      \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

/* Write `text` as `root/relative`, creating the directories above it. */
static void put(const char *root, const char *relative, const char *text) {
    char path[4096], *slash;
    FILE *file;

    snprintf(path, sizeof(path), "%s/%s", root, relative);
    for (slash = path + strlen(root) + 1; (slash = strchr(slash, '/')) != NULL;
         ++slash) {
        *slash = '\0';
        mkdir(path, 0755);
        *slash = '/';
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        ++failures;
        return;
    }
    fputs(text, file);
    fclose(file);
}

/* Verify the administrator and CPU job ceilings compose safely, and the CPU
 * budget a build derives from its cgroup and affinity mask. */
int main(void) {
    char root[4096];
    const char *source = NULL;
    long detected;

    CHECK(cbs_effective_jobs(8, 2, 4) == 2 && cbs_effective_jobs(2, 8, 1) == 1 &&
          cbs_effective_jobs(0, 0, 0) == 1);

    /* cpu.max: a quota over a period, rounded up, where `max` and noise say
     * nothing so the reader falls through to the next source. */
    CHECK(cbs_cpu_max_budget("800000 100000\n") == 8);
    CHECK(cbs_cpu_max_budget("100000 100000") == 1);
    CHECK(cbs_cpu_max_budget("50000 100000\n") == 1);
    CHECK(cbs_cpu_max_budget("150000 100000\n") == 2);
    CHECK(cbs_cpu_max_budget("max 100000\n") == 0);
    CHECK(cbs_cpu_max_budget("0 100000\n") == 0);
    CHECK(cbs_cpu_max_budget("800000 0\n") == 0);
    CHECK(cbs_cpu_max_budget("800000\n") == 0);
    CHECK(cbs_cpu_max_budget("eight cores\n") == 0);
    CHECK(cbs_cpu_max_budget("") == 0 && cbs_cpu_max_budget(NULL) == 0);

    /* Cpus_allowed_list: ranges and singletons, and a malformed list is 0
     * rather than a guess. */
    CHECK(cbs_cpu_list_count("0-7\n") == 8);
    CHECK(cbs_cpu_list_count("0-7,10,12-13\n") == 11);
    CHECK(cbs_cpu_list_count("3") == 1);
    CHECK(cbs_cpu_list_count("") == 0 && cbs_cpu_list_count(NULL) == 0);
    CHECK(cbs_cpu_list_count("7-3") == 0);
    CHECK(cbs_cpu_list_count("0-") == 0);
    CHECK(cbs_cpu_list_count("a") == 0);
    CHECK(cbs_cpu_list_count("0,,1") == 0);

    if (!test_temp_root(root, sizeof(root), "cbs-jobs"))
        return 1;

    /* The cgroup walk: the process's own limit, then each ancestor's, and the
     * smallest binds, since a parent's quota caps its children. */
    put(root, "own/a/b/cpu.max", "200000 100000\n");
    CHECK(cbs_cgroup_cpu_budget(root + 0, "0::/nothing/here\n") == 0);
    {
        char own[4096];
        snprintf(own, sizeof(own), "%s/own", root);
        CHECK(cbs_cgroup_cpu_budget(own, "0::/a/b\n") == 2);
        /* An unlimited child under a limited parent is limited. */
        put(root, "own/a/b/cpu.max", "max 100000\n");
        put(root, "own/a/cpu.max", "100000 100000\n");
        CHECK(cbs_cgroup_cpu_budget(own, "0::/a/b\n") == 1);
        /* The smaller of two limits wins, whichever level carries it. */
        put(root, "own/a/b/cpu.max", "400000 100000\n");
        put(root, "own/a/cpu.max", "300000 100000\n");
        CHECK(cbs_cgroup_cpu_budget(own, "0::/a/b\n") == 3);
        put(root, "own/a/b/cpu.max", "200000 100000\n");
        CHECK(cbs_cgroup_cpu_budget(own, "0::/a/b\n") == 2);
        /* The hybrid line is not the one that counts: cgroup v2 is `0::`. */
        CHECK(cbs_cgroup_cpu_budget(own, "12:cpu:/a/b\n0::/a/b\n") == 2);
        CHECK(cbs_cgroup_cpu_budget(own, "12:cpu:/a/b\n") == 0);
        /* A path that climbs out of the mount is refused, not followed. */
        CHECK(cbs_cgroup_cpu_budget(own, "0::/a/../b\n") == 0);
        CHECK(cbs_cgroup_cpu_budget(own, "") == 0);
        CHECK(cbs_cgroup_cpu_budget(own, "0::relative\n") == 0);
        /* The root of the hierarchy, where a container often sits. */
        put(root, "own/cpu.max", "100000 100000\n");
        CHECK(cbs_cgroup_cpu_budget(own, "0::/\n") == 1);
    }

    /* The real detection, on whatever host this is: all that can be asserted
     * without depending on it is that there is an answer, a source for it, and
     * that it is never below one. */
    detected = cbs_detect_cpu_budget(&source);
    CHECK(detected >= 1 && source != NULL && source[0] != '\0');
    CHECK(cbs_detect_cpu_budget(NULL) == detected);

    test_remove_tree(root);
    if (failures != 0)
        return 1;
    puts("jobs policy tests: PASS (shared ceiling, cpu.max, affinity list, "
         "cgroup walk)");
    return 0;
}
