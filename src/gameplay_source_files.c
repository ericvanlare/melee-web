#include "gameplay_source_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct SourceFile {
    char* name;
    const uint8_t* bytes;
    size_t size;
} SourceFile;

typedef struct SourceFileCopy {
    struct SourceFileCopy* next;
    const void* destination;
    const char* name;
} SourceFileCopy;

struct MeleeWebSourceFileScope {
    size_t count;
    SourceFile* files;
    SourceFileCopy* copies;
};

static MeleeWebSourceFileScope* active_scope;

static int fail(char* error, size_t error_size, const char* message)
{
    if (error && error_size) snprintf(error, error_size, "%s", message);
    return 0;
}

static void destroy_scope(MeleeWebSourceFileScope* scope)
{
    if (!scope) return;
    SourceFileCopy* copy = scope->copies;
    while (copy) {
        SourceFileCopy* next = copy->next;
        free(copy);
        copy = next;
    }
    for (size_t i = 0; i < scope->count; ++i) free(scope->files[i].name);
    free(scope->files);
    free(scope);
}

MeleeWebSourceFileScope* melee_web_source_files_begin(
    const MeleeWebSourceFileInput* input, size_t count, char* error,
    size_t error_size)
{
    if (active_scope) {
        fail(error, error_size, "A source RuntimeFiles scope is already active");
        return NULL;
    }
    if (count && !input) {
        fail(error, error_size, "Source RuntimeFiles input is missing");
        return NULL;
    }

    MeleeWebSourceFileScope* scope = calloc(1, sizeof(*scope));
    if (!scope) {
        fail(error, error_size, "Unable to allocate source RuntimeFiles scope");
        return NULL;
    }
    if (count > SIZE_MAX / sizeof(*scope->files)) {
        destroy_scope(scope);
        fail(error, error_size, "Source RuntimeFiles index size overflows");
        return NULL;
    }
    if (count) {
        scope->files = calloc(count, sizeof(*scope->files));
        if (!scope->files) {
            destroy_scope(scope);
            fail(error, error_size, "Unable to allocate source RuntimeFiles index");
            return NULL;
        }
    }

    for (size_t i = 0; i < count; ++i) {
        if (!input[i].name || !input[i].name[0] ||
            (!input[i].bytes && input[i].size)) {
            scope->count = i;
            destroy_scope(scope);
            fail(error, error_size, "Invalid source RuntimeFiles entry");
            return NULL;
        }
        const size_t name_size = strnlen(input[i].name, 4097);
        if (name_size > 4096) {
            scope->count = i;
            destroy_scope(scope);
            fail(error, error_size, "Source RuntimeFiles name exceeds 4096 bytes");
            return NULL;
        }
        for (size_t j = 0; j < i; ++j) {
            if (!strcmp(input[i].name, scope->files[j].name)) {
                scope->count = i;
                destroy_scope(scope);
                fail(error, error_size, "Duplicate exact source RuntimeFiles name");
                return NULL;
            }
        }
        scope->files[i].name = malloc(name_size + 1);
        if (!scope->files[i].name) {
            scope->count = i;
            destroy_scope(scope);
            fail(error, error_size, "Unable to copy source RuntimeFiles name");
            return NULL;
        }
        memcpy(scope->files[i].name, input[i].name, name_size + 1);
        scope->files[i].bytes = input[i].bytes;
        scope->files[i].size = input[i].size;
        scope->count = i + 1;
    }

    active_scope = scope;
    if (error && error_size) error[0] = '\0';
    return scope;
}

int melee_web_source_files_end(MeleeWebSourceFileScope* scope, char* error,
                               size_t error_size)
{
    if (!scope || active_scope != scope)
        return fail(error, error_size,
                    "Source RuntimeFiles scope is not the active owner");
    active_scope = NULL;
    destroy_scope(scope);
    if (error && error_size) error[0] = '\0';
    return 1;
}

static const SourceFile* find_file(const char* name)
{
    if (!active_scope || !name) return NULL;
    for (size_t i = 0; i < active_scope->count; ++i) {
        if (!strcmp(active_scope->files[i].name, name))
            return &active_scope->files[i];
    }
    /* Retail FST paths for root files begin with '/'; RuntimeFiles keys are
     * the same exact root-relative names without that DVD path marker. */
    if (name[0] == '/' && name[1] != '\0' && name[1] != '/') {
        const char* root_name = name + 1;
        for (size_t i = 0; i < active_scope->count; ++i) {
            if (!strcmp(active_scope->files[i].name, root_name))
                return &active_scope->files[i];
        }
    }
    return NULL;
}

int melee_web_source_file_size(const char* name, size_t* size)
{
    const SourceFile* file = find_file(name);
    if (!file || !size) return 0;
    *size = file->size;
    return 1;
}

int melee_web_source_file_copy(const char* name, void* destination,
                               size_t* size)
{
    const SourceFile* file = find_file(name);
    if (!file || !size || (file->size && !destination)) return 0;
    SourceFileCopy* copy = active_scope->copies;
    while (copy && copy->destination != destination) copy = copy->next;
    if (!copy) {
        copy = malloc(sizeof(*copy));
        if (!copy) return 0;
        copy->next = active_scope->copies;
        active_scope->copies = copy;
        copy->destination = destination;
    }
    copy->name = file->name;
    if (file->size) memcpy(destination, file->bytes, file->size);
    *size = file->size;
    return 1;
}

const char* melee_web_source_file_take_name(const void* copied_buffer)
{
    if (!active_scope || !copied_buffer) return NULL;
    SourceFileCopy** link = &active_scope->copies;
    while (*link && (*link)->destination != copied_buffer) link = &(*link)->next;
    if (!*link) return NULL;
    SourceFileCopy* copy = *link;
    *link = copy->next;
    const char* name = copy->name;
    free(copy);
    return name;
}
