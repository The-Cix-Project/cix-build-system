#define _POSIX_C_SOURCE 200809L

/* Regression tests for ELF DT_NEEDED dependency observation. */
#include "cbs.h"
#include "temp.h"
#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int saw_libc;
static int observed;
static int collect(const char *name, void *user) {
    (void)user;
    ++observed;
    if (strstr(name, "libc.so") != NULL)
        saw_libc = 1;
    return 1;
}

/* Write an ELF64 file with the given type and program header types and
 * nothing else, the smallest input that exercises the header rules.  `cut` bytes
 * are dropped from the end, to make a table that runs past the file. */
static int write_elf(const char *path, uint16_t type, const uint32_t *segments,
                     uint16_t count, size_t cut) {
    Elf64_Ehdr header;
    Elf64_Phdr segment;
    FILE *file;
    uint16_t index;
    long size;

    memset(&header, 0, sizeof(header));
    memcpy(header.e_ident, ELFMAG, SELFMAG);
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_ident[EI_VERSION] = EV_CURRENT;
    header.e_type = type;
    header.e_machine = EM_X86_64;
    header.e_version = EV_CURRENT;
    header.e_phoff = sizeof(header);
    header.e_ehsize = sizeof(header);
    header.e_phentsize = sizeof(segment);
    header.e_phnum = count;
    file = fopen(path, "wb");
    if (file == NULL || fwrite(&header, sizeof(header), 1, file) != 1)
        return 0;
    for (index = 0; index < count; ++index) {
        memset(&segment, 0, sizeof(segment));
        segment.p_type = segments[index];
        if (fwrite(&segment, sizeof(segment), 1, file) != 1)
            return 0;
    }
    if (fclose(file) != 0)
        return 0;
    if (cut == 0)
        return 1;
    size = (long)(sizeof(header) + (size_t)count * sizeof(segment));
    return truncate(path, size - (long)cut) == 0;
}

/* Observe one synthetic ELF and report whether it was accepted, having called
 * the observer for nothing: none of these files names a library. */
static int observe(const char *path) {
    observed = 0;
    return cbs_observe_dependencies(collect, path, NULL) && observed == 0;
}

/* Verify ELF dependency discovery and invalid-input rejection. */
int main(void) {
    char junk_path[4096];
    char elf_path[4096];
    const char junk[] = "not an ELF\n";
    const uint32_t static_exec[] = {PT_LOAD};
    const uint32_t static_with_stack[] = {PT_LOAD, PT_GNU_STACK};
    const uint32_t interpreter_only[] = {PT_INTERP, PT_LOAD};
    const uint32_t no_load[] = {PT_NOTE};
    int junk_fd;
    int elf_ok;
    int junk_rejected;
    int static_ok, static_stack_ok;
    int interp_refused, shared_refused, relocatable_refused, empty_refused,
        no_load_refused, truncated_refused;

    if (snprintf(junk_path, sizeof(junk_path), "%s/cbs-observe-junk-XXXXXX",
                 test_temp_dir()) >= (int)sizeof(junk_path) ||
        snprintf(elf_path, sizeof(elf_path), "%s/cbs-observe-elf",
                 test_temp_dir()) >= (int)sizeof(elf_path))
        return 1;
    junk_fd = mkstemp(junk_path);

    if (junk_fd < 0)
        return 1;
    if (write(junk_fd, junk, sizeof(junk) - 1) != (ssize_t)(sizeof(junk) - 1) ||
        close(junk_fd) != 0) {
        unlink(junk_path);
        return 1;
    }

    elf_ok = cbs_observe_dependencies(collect, "./cbs", NULL) && saw_libc;
    junk_rejected = !cbs_observe_dependencies(collect, junk_path, NULL);
    unlink(junk_path);

    /* A static executable links nothing, which `forbids` can rely on (#319,
     * ADR-0039).  It is accepted only on complete evidence, so each way a file
     * can look static while being something else is refused: a dynamic
     * executable that lost its dynamic section (it still names an interpreter),
     * a shared object without one, a relocatable object, a file with no program
     * headers, an executable with nothing loadable, and a header table that runs
     * past the end of the file. */
    static_ok = write_elf(elf_path, ET_EXEC, static_exec, 1, 0) &&
                observe(elf_path);
    static_stack_ok = write_elf(elf_path, ET_EXEC, static_with_stack, 2, 0) &&
                      observe(elf_path);
    interp_refused = write_elf(elf_path, ET_EXEC, interpreter_only, 2, 0) &&
                     !cbs_observe_dependencies(collect, elf_path, NULL);
    shared_refused = write_elf(elf_path, ET_DYN, static_exec, 1, 0) &&
                     !cbs_observe_dependencies(collect, elf_path, NULL);
    relocatable_refused = write_elf(elf_path, ET_REL, static_exec, 1, 0) &&
                          !cbs_observe_dependencies(collect, elf_path, NULL);
    empty_refused = write_elf(elf_path, ET_EXEC, NULL, 0, 0) &&
                    !cbs_observe_dependencies(collect, elf_path, NULL);
    no_load_refused = write_elf(elf_path, ET_EXEC, no_load, 1, 0) &&
                      !cbs_observe_dependencies(collect, elf_path, NULL);
    truncated_refused = write_elf(elf_path, ET_EXEC, static_with_stack, 2, 20) &&
                        !cbs_observe_dependencies(collect, elf_path, NULL);
    unlink(elf_path);

    if (!elf_ok || !junk_rejected || !static_ok || !static_stack_ok ||
        !interp_refused || !shared_refused || !relocatable_refused ||
        !empty_refused || !no_load_refused || !truncated_refused) {
        fprintf(stderr,
                "observe: elf=%d junk=%d static=%d static+stack=%d "
                "interp-refused=%d shared-refused=%d rel-refused=%d "
                "empty-refused=%d no-load-refused=%d truncated-refused=%d\n",
                elf_ok, junk_rejected, static_ok, static_stack_ok,
                interp_refused, shared_refused, relocatable_refused,
                empty_refused, no_load_refused, truncated_refused);
        return 1;
    }
    puts("ELF dependency observation tests: PASS (a dynamic ELF, non-ELF "
         "input, a static executable accepted, and six look-alikes refused)");
    return 0;
}
