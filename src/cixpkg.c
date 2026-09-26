/* CIXPKG v2 compression, verification, extraction, and serialization. */
#define _POSIX_C_SOURCE 200809L

#include "cbs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zstd.h>

#define CIXPKG_MAGIC "CIXPKG\0\2"
/* Identity occupies header bytes 160-223; byte 224 is the metadata flags. */
#define CIXPKG_IDENTITY_MAX 64
#define CIXPKG_FLAG_FINALIZED CBS_CIXPKG_FLAG_FINALIZED

/* Parse an octal mode retained in the typed manifest. Manifest construction
 * applies the privileged-file allowlist before this reader sees the mode. */
static int parse_mode(const char *text, unsigned *mode) {
    char extra;
    return sscanf(text, "%o %c", mode, &extra) == 1 &&
           *mode <= 07777U;
}

/* Check that a relative symlink remains inside the package root. */
static int safe_link_target(const char *relative, const char *target) {
    const char *p;
    int depth = 0;
    if (target[0] == '/')
        return 1;
    for (p = relative; *p != '\0'; ++p)
        if (*p == '/')
            ++depth;
    for (p = target; *p != '\0';) {
        const char *start = p;
        size_t length;
        while (*p == '/')
            ++p;
        start = p;
        while (*p != '\0' && *p != '/')
            ++p;
        length = (size_t)(p - start);
        if (length == 0 || (length == 1 && start[0] == '.'))
            continue;
        if (length == 2 && start[0] == '.' && start[1] == '.') {
            if (depth == 0)
                return 0;
            --depth;
        } else {
            ++depth;
        }
    }
    return 1;
}

static int decode_link_target(const char *hex, char *target,
                              size_t target_size) {
    size_t length = strlen(hex), i;
    if (length % 2 != 0 || length / 2 >= target_size)
        return 0;
    for (i = 0; i < length; i += 2) {
        unsigned value;
        if (sscanf(hex + i, "%2x", &value) != 1 || value == 0 || value > 255)
            return 0;
        target[i / 2] = (char)value;
    }
    target[length / 2] = '\0';
    return 1;
}

/* Store one little-endian 64-bit header field. */
static void put64(unsigned char *p, uint64_t value) {
    size_t i;
    for (i = 0; i < 8; ++i)
        p[i] = (unsigned char)(value >> (i * 8));
}

/* Load one little-endian 64-bit header field. */
static uint64_t get64(const unsigned char *p) {
    uint64_t value = 0;
    size_t i;
    for (i = 0; i < 8; ++i)
        value |= (uint64_t)p[i] << (i * 8);
    return value;
}

/* Read a complete file into a newly allocated byte buffer. */
static int read_blob(const char *path, unsigned char **data, size_t *size) {
    FILE *file;
    long length;
    *data = NULL;
    *size = 0;
    file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL)
            fclose(file);
        return 0;
    }
    *data = malloc((size_t)length == 0 ? 1 : (size_t)length);
    if (*data == NULL ||
        fread(*data, 1, (size_t)length, file) != (size_t)length ||
        fclose(file) != 0) {
        free(*data);
        *data = NULL;
        return 0;
    }
    *size = (size_t)length;
    return 1;
}

/* Append one regular-file payload to the concatenated payload buffer. */
static int append_blob(unsigned char **data, size_t *size, size_t *capacity,
                       const char *path) {
    unsigned char *part;
    size_t part_size, needed;
    if (!read_blob(path, &part, &part_size) || part_size > SIZE_MAX - *size) {
        free(part);
        return 0;
    }
    needed = *size + part_size;
    if (needed > *capacity) {
        size_t next = *capacity == 0 ? 4096 : *capacity;
        while (next < needed) {
            if (next > SIZE_MAX / 2) {
                free(part);
                return 0;
            }
            next *= 2;
        }
        *data = realloc(*data, next);
        if (*data == NULL) {
            free(part);
            return 0;
        }
        *capacity = next;
    }
    memcpy(*data + *size, part, part_size);
    *size = needed;
    free(part);
    return 1;
}

/* Concatenate regular-file bytes in canonical manifest order. */
static int make_payload(const char *manifest, const char *root,
                        unsigned char **payload, size_t *payload_size) {
    FILE *file;
    char line[8192], type, mode[32], digest[65], relative[4096], path[4096];
    unsigned long long size;
    size_t capacity = 0;
    /* Keep an addressable zero-length payload so its digest is well-defined
     * for a caller-assembled tree containing directories only. */
    *payload = malloc(1);
    if (*payload == NULL)
        return 0;
    *payload_size = 0;
    file = fopen(manifest, "rb");
    if (file == NULL)
        return 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (line[0] != 'f')
            continue;
        if (sscanf(line, "f %31s %*u %*u %llu %64s %4095[^\n]", mode, &size,
                   digest, relative) != 4 ||
            size > SIZE_MAX ||
            snprintf(path, sizeof(path), "%s/%s", root, relative) >=
                (int)sizeof(path) ||
            !append_blob(payload, payload_size, &capacity, path)) {
            fclose(file);
            free(*payload);
            *payload = NULL;
            return 0;
        }
    }
    if (ferror(file) || fclose(file) != 0) {
        free(*payload);
        *payload = NULL;
        return 0;
    }
    return 1;
}

/* Compress one uncompressed CIXPKG section with zstd. */
static int compress_blob(const unsigned char *input, size_t input_size,
                         unsigned char **output, size_t *output_size) {
    size_t bound = ZSTD_compressBound(input_size);
    *output = malloc(bound == 0 ? 1 : bound);
    if (*output == NULL)
        return 0;
    *output_size = ZSTD_compress(*output, bound, input, input_size, 19);
    if (ZSTD_isError(*output_size)) {
        free(*output);
        *output = NULL;
        return 0;
    }
    return 1;
}

/* Write a complete CIXPKG v2 header, manifest section, and payload section. */
int cbs_cixpkg_write_tree_with_flags(const char *manifest, const char *root,
                                     const char *package_path,
                                     const char *identity, unsigned flags) {
    unsigned char *manifest_data = NULL, *payload = NULL;
    unsigned char *manifest_compressed = NULL, *payload_compressed = NULL;
    size_t manifest_size, payload_size, manifest_compressed_size,
        payload_compressed_size;
    unsigned char header[352];
    char manifest_digest[65], payload_digest[65];
    FILE *file;
    size_t identity_length;
    int ok = 0;
    if (!manifest || !root || !package_path || !identity ||
        !read_blob(manifest, &manifest_data, &manifest_size) ||
        !make_payload(manifest, root, &payload, &payload_size) ||
        !cbs_digest_text((char *)manifest_data, manifest_size,
                         manifest_digest) ||
        !cbs_digest_text((char *)payload, payload_size, payload_digest) ||
        !compress_blob(manifest_data, manifest_size, &manifest_compressed,
                       &manifest_compressed_size) ||
        !compress_blob(payload, payload_size, &payload_compressed,
                       &payload_compressed_size))
        goto cleanup;
    memset(header, 0, sizeof(header));
    memcpy(header, CIXPKG_MAGIC, 8);
    put64(header + 8, sizeof(header));
    put64(header + 16, manifest_size);
    put64(header + 24, payload_size);
    memcpy(header + 32, manifest_digest, 64);
    memcpy(header + 96, payload_digest, 64);
    if (flags & ~CIXPKG_FLAG_FINALIZED)
        goto cleanup;
    header[224] = (unsigned char)flags;
    /* Identity occupies bytes 160-223. Truncating a longer identity would
     * make two packages share one name, and writing it past 223 would
     * overwrite the flags byte and produce an artifact this reader rejects,
     * so an identity that does not fit is a write failure. */
    identity_length = strlen(identity);
    if (identity_length > CIXPKG_IDENTITY_MAX)
        goto cleanup;
    memcpy(header + 160, identity, identity_length);
    file = fopen(package_path, "wb");
    if (file != NULL &&
        fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
        fwrite(manifest_compressed, 1, manifest_compressed_size, file) ==
            manifest_compressed_size &&
        fwrite(payload_compressed, 1, payload_compressed_size, file) ==
            payload_compressed_size &&
        fclose(file) == 0)
        ok = 1;
    else if (file != NULL)
        fclose(file);
cleanup:
    free(manifest_data);
    free(payload);
    free(manifest_compressed);
    free(payload_compressed);
    return ok;
}

/* Write a CIXPKG v2 artifact without embedder metadata flags. */
int cbs_cixpkg_write_tree(const char *manifest, const char *root,
                          const char *package_path, const char *identity) {
    return cbs_cixpkg_write_tree_with_flags(manifest, root, package_path,
                                            identity, 0);
}

/* Verify the entire artifact before exposing identity or manifest entries. */
int cbs_cixpkg_verify_tree(const char *package_path, char *identity,
                           size_t identity_size) {
    unsigned char *data, *manifest, *payload;
    size_t size, manifest_compressed_size, payload_compressed_size, written;
    uint64_t manifest_size, payload_size;
    char digest[65];
    FILE *file;
    if (!read_blob(package_path, &data, &size) || size < 352 ||
        memcmp(data, CIXPKG_MAGIC, 8) != 0 || get64(data + 8) != 352 ||
        (data[224] & ~CIXPKG_FLAG_FINALIZED) != 0 ||
        (manifest_size = get64(data + 16)) > 1024ULL * 1024ULL * 1024ULL ||
        (payload_size = get64(data + 24)) > 1024ULL * 1024ULL * 1024ULL) {
        free(data);
        return 0;
    }
    manifest_compressed_size =
        ZSTD_findFrameCompressedSize(data + 352, size - 352);
    if (ZSTD_isError(manifest_compressed_size) ||
        manifest_compressed_size > size - 352) {
        free(data);
        return 0;
    }
    payload_compressed_size = size - 352 - manifest_compressed_size;
    manifest = malloc((size_t)manifest_size + 1);
    payload = malloc((size_t)payload_size == 0 ? 1 : (size_t)payload_size);
    if (manifest == NULL || payload == NULL) {
        free(data);
        free(manifest);
        free(payload);
        return 0;
    }
    written = ZSTD_decompress(manifest, (size_t)manifest_size, data + 352,
                              manifest_compressed_size);
    if (ZSTD_isError(written) || written != (size_t)manifest_size ||
        !cbs_digest_text((char *)manifest, written, digest) ||
        memcmp(data + 32, digest, 64) != 0) {
        free(data);
        free(manifest);
        free(payload);
        return 0;
    }
    written = ZSTD_decompress(payload, (size_t)payload_size,
                              data + 352 + manifest_compressed_size,
                              payload_compressed_size);
    if (ZSTD_isError(written) || written != (size_t)payload_size ||
        !cbs_digest_text((char *)payload, written, digest) ||
        memcmp(data + 96, digest, 64) != 0) {
        free(data);
        free(manifest);
        free(payload);
        return 0;
    }
    if (identity != NULL && identity_size != 0) {
        size_t copy = identity_size - 1;
        if (copy > CIXPKG_IDENTITY_MAX)
            copy = CIXPKG_IDENTITY_MAX;
        memcpy(identity, data + 160, copy);
        identity[copy] = '\0';
    }
    file = fmemopen(manifest, (size_t)manifest_size, "rb");
    if (file == NULL) {
        free(data);
        free(manifest);
        free(payload);
        return 0;
    }
    {
        char line[8192], type, mode_text[32], entry_digest[65], relative[4096];
        char previous[4096] = {0};
        unsigned long long entry_size;
        unsigned uid, gid, mode;
        size_t offset = 0;
        int have_previous = 0;
        while (fgets(line, sizeof(line), file) != NULL) {
            if (sscanf(line, "%c", &type) != 1) {
                fclose(file);
                free(data);
                free(manifest);
                free(payload);
                return 0;
            }
            if (type == 'f') {
                if (sscanf(line, "f %31s %u %u %llu %64s %4095[^\n]", mode_text,
                           &uid, &gid, &entry_size, entry_digest,
                           relative) != 6 ||
                    !parse_mode(mode_text, &mode) || uid != 0 || gid != 0 ||
                    entry_size > (unsigned long long)payload_size ||
                    entry_size > payload_size - offset ||
                    !cbs_digest_text((char *)payload + offset,
                                     (size_t)entry_size, digest) ||
                    strcmp(digest, entry_digest) != 0) {
                    fclose(file);
                    free(data);
                    free(manifest);
                    free(payload);
                    return 0;
                }
                offset += (size_t)entry_size;
            } else if (type == 'd') {
                if (sscanf(line, "d %31s %u %u %4095[^\n]", mode_text, &uid,
                           &gid, relative) != 4 ||
                    !parse_mode(mode_text, &mode) || uid != 0 || gid != 0) {
                    fclose(file);
                    free(data);
                    free(manifest);
                    free(payload);
                    return 0;
                }
            } else if (type == 'l') {
                char target_hex[8192], target[4096];
                if (sscanf(line, "l %31s %u %u %8191s %4095[^\n]", mode_text,
                           &uid, &gid, target_hex, relative) != 5 ||
                    !parse_mode(mode_text, &mode) || uid != 0 || gid != 0 ||
                    !decode_link_target(target_hex, target, sizeof(target)) ||
                    !safe_link_target(relative, target)) {
                    fclose(file);
                    free(data);
                    free(manifest);
                    free(payload);
                    return 0;
                }
            } else if (type == 'm') {
                char key[32], value[4096];
                if (sscanf(line, "m %31s %4095[^\n]", key, value) != 2 ||
                    value[0] == '\0') {
                    fclose(file);
                    free(data);
                    free(manifest);
                    free(payload);
                    return 0;
                }
                continue;
            } else {
                fclose(file);
                free(data);
                free(manifest);
                free(payload);
                return 0;
            }
            if ((have_previous && strcmp(previous, relative) >= 0) ||
                !cbs_validate_stage_path(relative,
                                         &(CbsStagePolicy){1, 1, 1})) {
                fclose(file);
                free(data);
                free(manifest);
                free(payload);
                return 0;
            }
            strcpy(previous, relative);
            have_previous = 1;
        }
        if (ferror(file) || offset != (size_t)payload_size) {
            fclose(file);
            free(data);
            free(manifest);
            free(payload);
            return 0;
        }
    }
    fclose(file);
    free(data);
    free(manifest);
    free(payload);
    return 1;
}

int cbs_cixpkg_read_license(const char *package_path, char *license,
                            size_t license_size) {
    unsigned char *data = NULL, *manifest = NULL;
    size_t total, manifest_size, frame, written;
    char digest[65], line[8192], key[32], value[4096];
    FILE *file;

    if (license == NULL || license_size == 0 ||
        !read_blob(package_path, &data, &total) || total < 352 ||
        memcmp(data, CIXPKG_MAGIC, 8) != 0 || get64(data + 8) != 352 ||
        get64(data + 16) > 1024ULL * 1024ULL * 1024ULL) {
        free(data);
        return 0;
    }
    manifest_size = (size_t)get64(data + 16);
    frame = ZSTD_findFrameCompressedSize(data + 352, total - 352);
    manifest = malloc(manifest_size + 1);
    if (ZSTD_isError(frame) || frame > total - 352 || manifest == NULL ||
        ZSTD_isError(written = ZSTD_decompress(manifest, manifest_size,
                                                data + 352, frame)) ||
        written != manifest_size ||
        !cbs_digest_text((char *)manifest, written, digest) ||
        memcmp(data + 32, digest, 64) != 0) {
        free(manifest);
        free(data);
        return 0;
    }
    file = fmemopen(manifest, manifest_size, "rb");
    if (file == NULL) {
        free(manifest);
        free(data);
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "m %31s %4095[^\n]", key, value) == 2 &&
            strcmp(key, "license") == 0) {
            snprintf(license, license_size, "%s", value);
            fclose(file);
            free(manifest);
            free(data);
            return 1;
        }
    }
    fclose(file);
    free(manifest);
    free(data);
    license[0] = '\0';
    return 1;
}

/* Remove a temporary extraction tree recursively. */
static int remove_tree(const char *path) {
    DIR *directory = opendir(path);
    struct dirent *entry;
    char child[4096];
    if (directory == NULL)
        return unlink(path) == 0 || errno == ENOENT;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >=
                (int)sizeof(child) ||
            !remove_tree(child)) {
            closedir(directory);
            return 0;
        }
    }
    closedir(directory);
    return rmdir(path) == 0 || errno == ENOENT;
}

/* Create parent directories for one manifest entry. */
static int make_parent_dirs(const char *root, const char *relative) {
    char path[4096], *cursor;
    if (snprintf(path, sizeof(path), "%s/%s", root, relative) >=
        (int)sizeof(path))
        return 0;
    cursor = strrchr(path, '/');
    if (cursor == NULL)
        return 1;
    *cursor = '\0';
    for (cursor = path + strlen(root) + 1; *cursor != '\0'; ++cursor) {
        if (*cursor != '/')
            continue;
        *cursor = '\0';
        if (mkdir(path, 0700) != 0 && errno != EEXIST)
            return 0;
        *cursor = '/';
    }
    return mkdir(path, 0700) == 0 || errno == EEXIST;
}

/* Verify, populate, and atomically publish one extracted package tree. */
int cbs_cixpkg_extract(const char *package_path, const char *destination) {
    unsigned char *data = NULL, *manifest = NULL, *payload = NULL;
    size_t total, manifest_frame, manifest_compressed, payload_compressed;
    size_t manifest_size, payload_size, written, offset = 0;
    uint64_t manifest_length, payload_length;
    char temporary[4096] = {0}, line[8192], type, mode_text[32], digest[65],
         relative[4096];
    unsigned long long entry_size;
    FILE *file = NULL;
    int descriptor = -1, ok = 0;
    if (!package_path || !destination || strlen(destination) == 0 ||
        strlen(destination) > sizeof(temporary) - 32 ||
        access(destination, F_OK) == 0 ||
        !read_blob(package_path, &data, &total) || total < 352 ||
        memcmp(data, CIXPKG_MAGIC, 8) != 0 || get64(data + 8) != 352 ||
        (manifest_length = get64(data + 16)) > 1024ULL * 1024ULL * 1024ULL ||
        (payload_length = get64(data + 24)) > 1024ULL * 1024ULL * 1024ULL)
        goto cleanup;
    manifest_size = (size_t)manifest_length;
    payload_size = (size_t)payload_length;
    manifest_frame = ZSTD_findFrameCompressedSize(data + 352, total - 352);
    if (ZSTD_isError(manifest_frame) || manifest_frame > total - 352)
        goto cleanup;
    manifest_compressed = manifest_frame;
    payload_compressed = total - 352 - manifest_compressed;
    manifest = malloc(manifest_size + 1);
    payload = malloc(payload_size == 0 ? 1 : payload_size);
    if (!manifest || !payload)
        goto cleanup;
    written = ZSTD_decompress(manifest, manifest_size, data + 352,
                              manifest_compressed);
    if (ZSTD_isError(written) || written != manifest_size ||
        !cbs_digest_text((char *)manifest, written, digest) ||
        memcmp(data + 32, digest, 64) != 0)
        goto cleanup;
    written =
        ZSTD_decompress(payload, payload_size, data + 352 + manifest_compressed,
                        payload_compressed);
    if (ZSTD_isError(written) || written != payload_size ||
        !cbs_digest_text((char *)payload, written, digest) ||
        memcmp(data + 96, digest, 64) != 0)
        goto cleanup;
    if (snprintf(temporary, sizeof(temporary), "%s.cbs-tmp-XXXXXX",
                 destination) >= (int)sizeof(temporary))
        goto cleanup;
    descriptor = mkstemp(temporary);
    if (descriptor < 0 || close(descriptor) != 0 || unlink(temporary) != 0 ||
        mkdir(temporary, 0700) != 0)
        goto cleanup;
    file = fmemopen(manifest, manifest_size, "rb");
    if (!file)
        goto cleanup;
    {
        char previous[4096] = {0};
        int have_previous = 0;
        while (fgets(line, sizeof(line), file) != NULL) {
            char path[4096], entry_digest[65];
            unsigned mode;
            int output;
            if (sscanf(line, "%c", &type) != 1)
                goto cleanup;
            if (type == 'f') {
                unsigned uid, gid;
                int parsed =
                    sscanf(line, "f %31s %u %u %llu %64s %4095[^\n]", mode_text,
                           &uid, &gid, &entry_size, entry_digest, relative);
                if (parsed != 6 || !parse_mode(mode_text, &mode) || uid != 0 ||
                    gid != 0 || entry_size > payload_size ||
                    entry_size > payload_size - offset ||
                    !cbs_digest_text((char *)payload + offset,
                                     (size_t)entry_size, digest) ||
                    strcmp(digest, entry_digest) != 0 ||
                    snprintf(path, sizeof(path), "%s/%s", temporary,
                             relative) >= (int)sizeof(path) ||
                    !make_parent_dirs(temporary, relative))
                    goto cleanup;
            } else if (type == 'd') {
                unsigned uid, gid;
                if (sscanf(line, "d %31s %u %u %4095[^\n]", mode_text, &uid,
                           &gid, relative) != 4 ||
                    !parse_mode(mode_text, &mode) || uid != 0 || gid != 0 ||
                    snprintf(path, sizeof(path), "%s/%s", temporary,
                             relative) >= (int)sizeof(path) ||
                    !cbs_validate_stage_path(relative,
                                             &(CbsStagePolicy){1, 1, 1}) ||
                    !make_parent_dirs(temporary, relative))
                    goto cleanup;
                if (mkdir(path, mode & 07777) != 0 && errno != EEXIST)
                    goto cleanup;
                offset += 0;
                if (have_previous && strcmp(previous, relative) >= 0)
                    goto cleanup;
                strcpy(previous, relative);
                have_previous = 1;
                continue;
            } else if (type == 'l') {
                char target_hex[8192], target[4096];
                unsigned uid, gid;
                if (sscanf(line, "l %31s %u %u %8191s %4095[^\n]", mode_text,
                           &uid, &gid, target_hex, relative) != 5 ||
                    !parse_mode(mode_text, &mode) || uid != 0 || gid != 0 ||
                    !decode_link_target(target_hex, target, sizeof(target)) ||
                    !cbs_validate_stage_path(relative,
                                             &(CbsStagePolicy){1, 1, 1}) ||
                    snprintf(path, sizeof(path), "%s/%s", temporary,
                             relative) >= (int)sizeof(path) ||
                    !make_parent_dirs(temporary, relative))
                    goto cleanup;
                if (!safe_link_target(relative, target))
                    goto cleanup;
                if (symlink(target, path) != 0)
                    goto cleanup;
                if (have_previous && strcmp(previous, relative) >= 0)
                    goto cleanup;
                strcpy(previous, relative);
                have_previous = 1;
                continue;
            } else if (type == 'm') {
                char key[32], value[4096];
                if (sscanf(line, "m %31s %4095[^\n]", key, value) != 2 ||
                    value[0] == '\0')
                    goto cleanup;
                continue;
            } else
                goto cleanup;
            if (have_previous && strcmp(previous, relative) >= 0 ||
                !cbs_validate_stage_path(relative, &(CbsStagePolicy){1, 1, 1}))
                goto cleanup;
            strcpy(previous, relative);
            have_previous = 1;
            output = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW,
                          mode & 07777);
            if (output < 0 ||
                write(output, payload + offset, (size_t)entry_size) !=
                    (ssize_t)entry_size ||
                fchmod(output, mode & 07777) != 0 || close(output) != 0) {
                if (output >= 0)
                    close(output);
                goto cleanup;
            }
            offset += (size_t)entry_size;
        }
    }
    if (ferror(file) || offset != payload_size)
        goto cleanup;
    fclose(file);
    file = NULL;
    if (rename(temporary, destination) != 0)
        goto cleanup;
    ok = 1;
cleanup:
    if (file)
        fclose(file);
    if (descriptor >= 0)
        close(descriptor);
    if (!ok && temporary[0] != '\0')
        remove_tree(temporary);
    free(data);
    free(manifest);
    free(payload);
    return ok;
}
