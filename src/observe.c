/* ELF dynamic-section observation for runtime dependency discovery. */
#define _POSIX_C_SOURCE 200809L
#include "cbs.h"
#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int range_ok(size_t total, uint64_t offset, uint64_t length) {
    return offset <= total && length <= (uint64_t)total - offset;
}

static int vaddr_file(const unsigned char *data, size_t size, uint64_t phoff,
                      uint16_t entsize, uint16_t count, uint64_t address,
                      uint64_t *offset) {
    uint16_t i;
    for (i = 0; i < count; ++i) {
        const Elf64_Phdr *p;
        if (!range_ok(size, phoff + (uint64_t)i * entsize, entsize))
            return 0;
        p = (const Elf64_Phdr *)(data + phoff + (uint64_t)i * entsize);
        if (p->p_type != PT_LOAD || address < p->p_vaddr ||
            address - p->p_vaddr >= p->p_memsz)
            continue;
        *offset = p->p_offset + address - p->p_vaddr;
        return range_ok(size, *offset, 1);
    }
    return 0;
}

int cbs_observe_dependencies(CbsDependencyObserver observer, const char *path,
                             void *user) {
    FILE *file;
    long length;
    unsigned char *data;
    const Elf64_Ehdr *header;
    const Elf64_Phdr *dynamic = NULL;
    uint64_t string_address = 0, string_size = 0, string_offset;
    uint16_t i;
    int result = 0;
    if (observer == NULL || path == NULL ||
        (file = fopen(path, "rb")) == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) < (long)EI_NIDENT ||
        fseek(file, 0, SEEK_SET) != 0) {
        if (file)
            fclose(file);
        return 0;
    }
    data = malloc((size_t)length);
    if (data == NULL ||
        fread(data, 1, (size_t)length, file) != (size_t)length ||
        fclose(file) != 0) {
        free(data);
        return 0;
    }
    if ((size_t)length < sizeof(Elf64_Ehdr)) {
        free(data);
        return 0;
    }
    header = (const Elf64_Ehdr *)data;
    if (memcmp(data, ELFMAG, SELFMAG) != 0 || data[EI_CLASS] != ELFCLASS64 ||
        data[EI_DATA] != ELFDATA2LSB ||
        header->e_phentsize != sizeof(Elf64_Phdr) ||
        !range_ok((size_t)length, header->e_phoff,
                  (uint64_t)header->e_phentsize * header->e_phnum))
        goto done;
    for (i = 0; i < header->e_phnum; ++i) {
        const Elf64_Phdr *p =
            (const Elf64_Phdr *)(data + header->e_phoff +
                                 (uint64_t)i * header->e_phentsize);
        if (p->p_type == PT_DYNAMIC) {
            dynamic = p;
            break;
        }
    }
    if (dynamic == NULL || dynamic->p_filesz % sizeof(Elf64_Dyn) != 0 ||
        !range_ok((size_t)length, dynamic->p_offset, dynamic->p_filesz))
        goto done;
    for (i = 0; i < dynamic->p_filesz / sizeof(Elf64_Dyn); ++i) {
        const Elf64_Dyn *entry =
            (const Elf64_Dyn *)(data + dynamic->p_offset +
                                (uint64_t)i * sizeof(Elf64_Dyn));
        if (entry->d_tag == DT_STRTAB)
            string_address = entry->d_un.d_ptr;
        if (entry->d_tag == DT_STRSZ)
            string_size = entry->d_un.d_val;
    }
    if (string_size == 0 ||
        !vaddr_file(data, (size_t)length, header->e_phoff, header->e_phentsize,
                    header->e_phnum, string_address, &string_offset) ||
        !range_ok((size_t)length, string_offset, string_size))
        goto done;
    for (i = 0; i < dynamic->p_filesz / sizeof(Elf64_Dyn); ++i) {
        const Elf64_Dyn *entry =
            (const Elf64_Dyn *)(data + dynamic->p_offset +
                                (uint64_t)i * sizeof(Elf64_Dyn));
        uint64_t name = entry->d_un.d_val;
        if (entry->d_tag == DT_NEEDED &&
            (name >= string_size ||
             memchr(data + string_offset + name, '\0',
                    (size_t)(string_size - name)) == NULL ||
             !observer((const char *)(data + string_offset + name), user)))
            goto done;
    }
    result = 1;
done:
    free(data);
    return result;
}
