#include "gameplay_source_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

EM_JS(int, source_stream_start, (int request, const char* name,
                                uint32_t offset, uint32_t size), {
    if (typeof window.menuStartSourceRead !== 'function') return 0;
    return window.menuStartSourceRead(request, UTF8ToString(name), offset, size)
        ? 1 : 0;
});
EM_JS(int, source_stream_status, (int request), {
    if (typeof window.menuSourceReadStatus !== 'function') return -1;
    return window.menuSourceReadStatus(request);
});
EM_JS(uint32_t, source_stream_take, (int request), {
    if (typeof window.menuSourceReadTake !== 'function') return 0;
    return window.menuSourceReadTake(request);
});
EM_JS(void, source_stream_discard, (int request), {
    if (typeof window.menuSourceReadDiscard === 'function')
        window.menuSourceReadDiscard(request);
});
#endif

typedef struct SourceFile {
    char* name;
    const uint8_t* bytes;
    size_t size;
    const uint8_t* transfer_tail;
    size_t transfer_tail_size;
    int external;
} SourceFile;

typedef struct ExternalSourceFile {
    struct ExternalSourceFile* next;
    char* name;
    size_t size;
} ExternalSourceFile;

typedef struct SourceFileCopy {
    struct SourceFileCopy* next;
    const void* destination;
    const char* name;
    size_t extent;
    int ready;
} SourceFileCopy;

typedef struct SourceFileRegion {
    struct SourceFileRegion* next;
    uintptr_t begin;
    size_t size;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    uint64_t identity;
#endif
} SourceFileRegion;

typedef struct SourceFileRequest {
    struct SourceFileRequest* next;
    const SourceFile* file;
    uintptr_t offset;
    uintptr_t destination;
    size_t size;
    int id;
    int cancelled;
    int stream_dispatched;
    int stream_ready;
    MeleeWebSourceFileCallback callback;
} SourceFileRequest;

struct MeleeWebSourceFileScope {
    size_t count;
    SourceFile* files;
    SourceFileCopy* copies;
    SourceFileRegion* regions;
    SourceFileRequest* requests;
    SourceFileRequest** request_tail;
    size_t request_count;
    int pumping;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    struct {
        MeleeWebSourceFileRecordedCompletionPlan plan;
        MeleeWebSourceFileRecordedCompletionStatus status;
        const SourceFile* file;
        SourceFileRegion* region;
        uint64_t region_identity;
        int cursor_seen, committing;
    } recorded;
#endif
};

static MeleeWebSourceFileScope* active_scope;
static ExternalSourceFile* external_files;
static size_t external_file_count;
/* The bank transport owns 0x20000000..0x3fffffff. Keep source DVD requests
 * above that range so HSD_DevComCancelEx can dispatch them to the right
 * owner without relying on which owner happens to be checked first. */
static int next_source_request = 0x50000000;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
static uint64_t next_region_identity=1;
#endif

static int fail(char* error, size_t error_size, const char* message)
{
    if (error && error_size) snprintf(error, error_size, "%s", message);
    return 0;
}

static int source_file_ranges_overlap(uintptr_t first, size_t first_size,
                                      uintptr_t second, size_t second_size)
{
    if (!first_size || !second_size) return 0;
    if (first > UINTPTR_MAX - first_size ||
        second > UINTPTR_MAX - second_size) return 1;
    return first < second + second_size && second < first + first_size;
}

static void source_file_copy_forget_overlaps(MeleeWebSourceFileScope* scope,
                                             uintptr_t destination,
                                             size_t extent)
{
    SourceFileCopy** link = &scope->copies;
    while (*link) {
        SourceFileCopy* copy = *link;
        if ((uintptr_t)copy->destination == destination ||
            source_file_ranges_overlap((uintptr_t)copy->destination,
                                       copy->extent, destination, extent)) {
            *link = copy->next;
            free(copy);
        } else {
            link = &copy->next;
        }
    }
}

static SourceFileCopy* source_file_copy_prepare(
    MeleeWebSourceFileScope* scope, uintptr_t destination)
{
    SourceFileCopy* copy = malloc(sizeof(*copy));
    if (!copy) return NULL;
    copy->next = scope->copies;
    copy->destination = (const void*)destination;
    copy->name = NULL;
    copy->extent = 0;
    copy->ready = 0;
    scope->copies = copy;
    return copy;
}

static void source_file_copy_commit(SourceFileCopy* copy,
                                    const SourceFile* file, size_t extent)
{
    copy->name = file->name;
    copy->extent = extent;
    copy->ready = 1;
}

static int source_file_transfer_extent(size_t logical_size,
                                       size_t tail_size, size_t* extent)
{
    if (!extent) return 0;
    if (!tail_size) {
        *extent = logical_size;
        return 1;
    }
    if (logical_size > SIZE_MAX - 31u || tail_size > 31u) return 0;
    const size_t rounded = (logical_size + 31u) & ~(size_t)31u;
    if (rounded < logical_size || tail_size != rounded - logical_size ||
        logical_size > SIZE_MAX - tail_size) {
        return 0;
    }
    *extent = logical_size + tail_size;
    return *extent == rounded;
}

static int source_file_input_tail_valid(const uint8_t* tail,
                                        size_t tail_size,
                                        size_t logical_size)
{
    size_t extent = 0;
    if (!source_file_transfer_extent(logical_size, tail_size, &extent))
        return 0;
    if (!tail_size) return 1;
    if (!tail || (uintptr_t)tail > UINTPTR_MAX - tail_size) return 0;
    return 1;
}

static size_t source_file_transfer_size(const SourceFile* file)
{
    return file->size + file->transfer_tail_size;
}

static int valid_external_name(const char* name, size_t* length)
{
    if (!name || !name[0]) return 0;
    const size_t size = strnlen(name, 4097);
    if (!size || size > 4096 || name[0] == '/' || name[0] == '\\')
        return 0;
    size_t component_start = 0;
    for (size_t i = 0; i <= size; ++i) {
        const unsigned char c = (unsigned char)name[i];
        if (i != size && (c < 0x20 || c == ':' || c == '\\')) return 0;
        if (i == size || c == '/') {
            const size_t component_size = i - component_start;
            if (!component_size ||
                (component_size == 1 && name[component_start] == '.') ||
                (component_size == 2 && name[component_start] == '.' &&
                 name[component_start + 1] == '.')) return 0;
            component_start = i + 1;
        }
    }
    if (length) *length = size;
    return 1;
}

static ExternalSourceFile* find_external_file(const char* name)
{
    for (ExternalSourceFile* file = external_files; file; file = file->next)
        if (!strcmp(file->name, name)) return file;
    return NULL;
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
    SourceFileRegion* region = scope->regions;
    while (region) {
        SourceFileRegion* next = region->next;
        free(region);
        region = next;
    }
    SourceFileRequest* request = scope->requests;
    while (request) {
        SourceFileRequest* next = request->next;
        free(request);
        request = next;
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
    if (external_file_count > SIZE_MAX - count ||
        count + external_file_count >
            (size_t)(INT_MAX - MELEE_WEB_SOURCE_FILE_ENTRY_BASE)) {
        fail(error, error_size, "Source RuntimeFiles entry range is exhausted");
        return NULL;
    }
    const size_t total_count = count + external_file_count;

    MeleeWebSourceFileScope* scope = calloc(1, sizeof(*scope));
    if (!scope) {
        fail(error, error_size, "Unable to allocate source RuntimeFiles scope");
        return NULL;
    }
    if (total_count > SIZE_MAX / sizeof(*scope->files)) {
        destroy_scope(scope);
        fail(error, error_size, "Source RuntimeFiles index size overflows");
        return NULL;
    }
    if (total_count) {
        scope->files = calloc(total_count, sizeof(*scope->files));
        if (!scope->files) {
            destroy_scope(scope);
            fail(error, error_size, "Unable to allocate source RuntimeFiles index");
            return NULL;
        }
    }
    scope->request_tail = &scope->requests;

    for (size_t i = 0; i < total_count; ++i) {
        const char* name;
        const uint8_t* bytes;
        size_t size;
        const uint8_t* transfer_tail;
        size_t transfer_tail_size;
        int is_external = i >= count;
        if (is_external) {
            size_t external_index = i - count;
            ExternalSourceFile* external = external_files;
            while (external && external_index--) external = external->next;
            if (!external) {
                scope->count = i;
                destroy_scope(scope);
                fail(error, error_size, "External source file catalog changed during scope startup");
                return NULL;
            }
            name = external->name;
            bytes = NULL;
            size = external->size;
            transfer_tail = NULL;
            transfer_tail_size = 0;
        } else {
            name = input[i].name;
            bytes = input[i].bytes;
            size = input[i].size;
            transfer_tail = input[i].transfer_tail;
            transfer_tail_size = input[i].transfer_tail_size;
        }
        if (!name || !name[0] || (!bytes && size && !is_external) ||
            (bytes && (uintptr_t)bytes > UINTPTR_MAX - size) ||
            !source_file_input_tail_valid(transfer_tail,
                                          transfer_tail_size, size)) {
            scope->count = i;
            destroy_scope(scope);
            fail(error, error_size,
                 "Invalid source RuntimeFiles entry or transfer tail");
            return NULL;
        }
        const size_t name_size = strnlen(name, 4097);
        if (name_size > 4096) {
            scope->count = i;
            destroy_scope(scope);
            fail(error, error_size, "Source RuntimeFiles name exceeds 4096 bytes");
            return NULL;
        }
        for (size_t j = 0; j < i; ++j) {
            if (!strcmp(name, scope->files[j].name)) {
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
        memcpy(scope->files[i].name, name, name_size + 1);
        scope->files[i].bytes = bytes;
        scope->files[i].size = size;
        scope->files[i].transfer_tail = transfer_tail;
        scope->files[i].transfer_tail_size = transfer_tail_size;
        scope->files[i].external = is_external;
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
    if (scope->pumping || scope->requests || scope->regions) {
        return fail(error, error_size,
                    "Source RuntimeFiles still owns requests or writable regions");
    }
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
    if (!file || file->external || !size || (file->size && !destination)) return 0;
    const uintptr_t destination_address = (uintptr_t)destination;
    if (file->size && destination_address > UINTPTR_MAX - file->size)
        return 0;
    source_file_copy_forget_overlaps(active_scope, destination_address,
                                     file->size);
    SourceFileCopy* copy = source_file_copy_prepare(active_scope,
                                                    destination_address);
    if (!copy) return 0;
    if (file->size) memcpy(destination, file->bytes, file->size);
    source_file_copy_commit(copy, file, file->size);
    *size = file->size;
    return 1;
}

const char* melee_web_source_file_take_name(const void* copied_buffer)
{
    if (!active_scope || !copied_buffer) return NULL;
    SourceFileCopy** link = &active_scope->copies;
    while (*link && (*link)->destination != copied_buffer) link = &(*link)->next;
    if (!*link || !(*link)->ready) return NULL;
    SourceFileCopy* copy = *link;
    *link = copy->next;
    const char* name = copy->name;
    free(copy);
    return name;
}

static const SourceFile* file_for_entry(int entry)
{
    if (!active_scope || entry < MELEE_WEB_SOURCE_FILE_ENTRY_BASE) return NULL;
    const size_t index = (size_t)(entry - MELEE_WEB_SOURCE_FILE_ENTRY_BASE);
    return index < active_scope->count ? &active_scope->files[index] : NULL;
}

int melee_web_source_file_entry_size(int entry, size_t* size)
{
    const SourceFile* file = file_for_entry(entry);
    if (!file || !size) return 0;
    *size = file->size;
    return 1;
}

int melee_web_source_file_entry(const char* name)
{
    const SourceFile* file = find_file(name);
    if (!file) return -1;
    return MELEE_WEB_SOURCE_FILE_ENTRY_BASE +
           (int)(file - active_scope->files);
}

int melee_web_source_file_entry_owned(int entry)
{
    return file_for_entry(entry) != NULL;
}

int melee_web_source_files_active(void)
{
    return active_scope != NULL;
}

MeleeWebSourceFileScope* melee_web_source_files_current_scope(void)
{
    return active_scope;
}

int melee_web_source_file_drive_busy(void)
{
    return active_scope && active_scope->request_count != 0;
}

int melee_web_source_files_external_set(const char* name, size_t size,
                                        char* error, size_t error_size)
{
    if (active_scope)
        return fail(error, error_size,
                    "Cannot change external files while a source scope is active");
    size_t name_size = 0;
    if (!valid_external_name(name, &name_size) || !size || size > UINT32_MAX)
        return fail(error, error_size, "Invalid external source file metadata");
    if (find_external_file(name))
        return fail(error, error_size, "Duplicate external source file name");
    if (external_file_count >= 64)
        return fail(error, error_size, "External source file catalog is full");
    ExternalSourceFile* file = calloc(1, sizeof(*file));
    if (!file) return fail(error, error_size,
                           "Unable to allocate external source file entry");
    file->name = malloc(name_size + 1);
    if (!file->name) {
        free(file);
        return fail(error, error_size,
                    "Unable to copy external source file name");
    }
    memcpy(file->name, name, name_size + 1);
    file->size = size;
    file->next = external_files;
    external_files = file;
    ++external_file_count;
    if (error && error_size) error[0] = '\0';
    return 1;
}

int melee_web_source_files_external_clear(char* error, size_t error_size)
{
    if (active_scope)
        return fail(error, error_size,
                    "Cannot clear external files while a source scope is active");
    ExternalSourceFile* file = external_files;
    while (file) {
        ExternalSourceFile* next = file->next;
        free(file->name);
        free(file);
        file = next;
    }
    external_files = NULL;
    external_file_count = 0;
    if (error && error_size) error[0] = '\0';
    return 1;
}

int melee_web_source_file_region_register(void* base, size_t size,
                                          char* error, size_t error_size)
{
    if (!active_scope || !base || !size ||
        (uintptr_t)base > UINTPTR_MAX - size) {
        return fail(error, error_size,
                    "Invalid source RuntimeFiles writable region");
    }
    const uintptr_t begin = (uintptr_t)base;
    const uintptr_t end = begin + size;
    for (SourceFileRegion* at = active_scope->regions; at; at = at->next) {
        const uintptr_t at_end = at->begin + at->size;
        if (begin < at_end && at->begin < end) {
            return fail(error, error_size,
                        "Overlapping source RuntimeFiles writable regions");
        }
    }
    SourceFileRegion* region = malloc(sizeof(*region));
    if (!region) {
        return fail(error, error_size,
                    "Unable to allocate source RuntimeFiles region owner");
    }
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if (!next_region_identity) { free(region); return fail(error,error_size,"Source region identity exhausted"); }
    region->identity=next_region_identity++;
#endif
    region->begin = begin;
    region->size = size;
    region->next = active_scope->regions;
    active_scope->regions = region;
    if (error && error_size) error[0] = '\0';
    return 1;
}

int melee_web_source_file_region_unregister(void* base, size_t size,
                                            char* error, size_t error_size)
{
    if (!active_scope || !base || !size ||
        (uintptr_t)base > UINTPTR_MAX - size) {
        return fail(error, error_size,
                    "Invalid source RuntimeFiles region release");
    }
    const uintptr_t begin = (uintptr_t)base;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if(active_scope->recorded.status.armed&&(!active_scope->recorded.status.completed||active_scope->recorded.status.failed))
        return fail(error,error_size,"Recorded source region remains owned by incomplete gate");
#endif
    SourceFileRegion** link = &active_scope->regions;
    while (*link && ((*link)->begin != begin || (*link)->size != size))
        link = &(*link)->next;
    if (!*link) {
        return fail(error, error_size,
                    "Source RuntimeFiles region release lost its owner");
    }
    const uintptr_t end = begin + size;
    for (SourceFileRequest* request = active_scope->requests; request;
         request = request->next) {
        const uintptr_t request_end = request->destination + request->size;
        if (request->destination < end && begin < request_end) {
            return fail(error, error_size,
                        "Source RuntimeFiles region still has a pending request");
        }
    }
    SourceFileRegion* region = *link;
    *link = region->next;
    free(region);
    if (error && error_size) error[0] = '\0';
    return 1;
}

static int destination_owned(uintptr_t destination, size_t size)
{
    if (!active_scope || destination > UINTPTR_MAX - size) return 0;
    for (SourceFileRegion* region = active_scope->regions; region;
         region = region->next) {
        if (destination >= region->begin &&
            destination - region->begin <= region->size &&
            size <= region->size - (destination - region->begin)) {
            return 1;
        }
    }
    return 0;
}

static int source_file_copy_transfer(const SourceFile* file,
                                    uintptr_t offset, void* destination,
                                    size_t size)
{
    const size_t extent = source_file_transfer_size(file);
    if (offset > extent || size > extent - (size_t)offset ||
        (size && !destination)) {
        return 0;
    }
    const size_t start = (size_t)offset;
    size_t logical_bytes = 0;
    if (start < file->size) {
        logical_bytes = file->size - start;
        if (logical_bytes > size) logical_bytes = size;
    }
    const size_t tail_bytes = size - logical_bytes;
    size_t tail_offset = 0;
    if (tail_bytes) {
        tail_offset = start + logical_bytes - file->size;
        if (tail_offset > file->transfer_tail_size ||
            tail_bytes > file->transfer_tail_size - tail_offset) {
            return 0;
        }
    }
    if (logical_bytes) {
        memcpy(destination, file->bytes + start, logical_bytes);
    }
    if (tail_bytes) {
        memcpy((uint8_t*)destination + logical_bytes,
               file->transfer_tail + tail_offset, tail_bytes);
    }
    return 1;
}

#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
static int recorded_fail(MeleeWebSourceFileScope* scope,char* e,size_t n,const char* message) {
    scope->recorded.status.failed=1; return fail(e,n,message);
}
static int recorded_region_live(MeleeWebSourceFileScope* scope) {
    for(SourceFileRegion* r=scope->regions;r;r=r->next)
        if(r==scope->recorded.region)
            return r->identity==scope->recorded.region_identity &&
                r->begin==(uintptr_t)scope->recorded.plan.region_base &&
                r->size==scope->recorded.plan.region_size;
    return 0; /* Do not dereference a released region token. */
}
int melee_web_source_files_recorded_completion_arm(MeleeWebSourceFileScope* scope,
    const MeleeWebSourceFileRecordedCompletionPlan* p,char* e,size_t n) {
    if(!scope||scope!=active_scope||!p||scope->pumping||scope->requests||
       scope->recorded.status.armed||!p->name||!p->callback_matches||
       p->request_cursor>=p->callback_cursor||p->callback_cursor>p->last_selected_cursor||
       p->last_selected_cursor>UINT32_MAX||p->offset!=0||
       (p->type!=0x21&&p->type!=0x23)||p->priority!=1)
        return fail(e,n,"Recorded source completion plan/owner refused");
    const SourceFile* file=file_for_entry(p->entry);
    if(!file||file->external||!file->bytes||strcmp(file->name,p->name)||file->size!=p->logical_size||
       !p->logical_size||source_file_transfer_size(file)!=p->transfer_size||
       !p->transfer_size||p->transfer_size%32||
       !destination_owned(p->destination,p->transfer_size))
        return fail(e,n,"Recorded source completion file/extent refused");
    SourceFileRegion* region=NULL;
    for(SourceFileRegion* r=scope->regions;r;r=r->next)
        if(r->begin==(uintptr_t)p->region_base&&r->size==p->region_size)region=r;
    if(!region||p->destination<region->begin||p->destination-region->begin>region->size||
       p->transfer_size>region->size-(p->destination-region->begin))
        return fail(e,n,"Recorded source completion region refused");
    scope->recorded.plan=*p; scope->recorded.file=file;
    scope->recorded.region=region;scope->recorded.region_identity=region->identity;
    scope->recorded.status.armed=1;
    if(e&&n)*e=0;return 1;
}
/* Resident RuntimeFiles own the already authenticated logical bytes/tail.
 * Readiness needs no I/O: defer the unchanged copy/receipt/callback path. */
static int recorded_pending_matches(MeleeWebSourceFileScope* scope,char* e,size_t n) {
    if(scope->recorded.status.failed)return fail(e,n,"Recorded source completion already failed");
    if(!recorded_region_live(scope))return recorded_fail(scope,e,n,"Recorded source region lease changed");
    if(!scope->recorded.status.request_seen)return 1;
    SourceFileRequest* r=scope->requests;
    if(!r||r->next||scope->request_count!=1||r->id!=scope->recorded.status.request_id||
       r->cancelled||r->file!=scope->recorded.file||r->file->external||!r->file->bytes)
        return recorded_fail(scope,e,n,"Recorded resident FIFO/request changed");
    return 1;
}
int melee_web_source_files_recorded_completion_status(MeleeWebSourceFileScope* scope,
    MeleeWebSourceFileRecordedCompletionStatus* out,char* e,size_t n) {
    if(!scope||scope!=active_scope||!out||!scope->recorded.status.armed)
        return fail(e,n,"Recorded completion status owner refused");
    *out=scope->recorded.status;if(e&&n)*e=0;return 1;
}
int melee_web_source_files_recorded_preconsume(MeleeWebSourceFileScope* scope,
    uint64_t cursor,char* e,size_t n) {
    if(!scope||scope!=active_scope||!scope->recorded.status.armed||scope->pumping)
        return fail(e,n,"Recorded pre-consume owner refused");
    MeleeWebSourceFileRecordedCompletionStatus* st=&scope->recorded.status;
    const MeleeWebSourceFileRecordedCompletionPlan* p=&scope->recorded.plan;
    if(st->failed||(!scope->recorded.cursor_seen&&cursor!=0)||
       (scope->recorded.cursor_seen&&(st->last_preconsume_cursor==UINT64_MAX||cursor!=st->last_preconsume_cursor+1)))
        return recorded_fail(scope,e,n,"Recorded pre-consume cursor skipped/repeated");
    scope->recorded.cursor_seen=1;st->last_preconsume_cursor=cursor;
    if(st->completed){if(e&&n)*e=0;return 1;}
    if(cursor>p->request_cursor&&!st->request_seen)
        return recorded_fail(scope,e,n,"Recorded request cursor missed");
    if(!recorded_pending_matches(scope,e,n))return 0;
    if(cursor<p->callback_cursor){if(e&&n)*e=0;return 1;}
    if(cursor!=p->callback_cursor||!st->request_seen)
        return recorded_fail(scope,e,n,"Recorded callback cursor missed");
    scope->recorded.committing=1;
    int completed=melee_web_source_files_pump_for(scope,e,n);
    scope->recorded.committing=0;
    if(!completed||scope->requests||scope->request_count)
        return recorded_fail(scope,e,n,"Recorded completion did not settle exact FIFO");
    st->completed=1;st->actual_callback_cursor=cursor;
    return 1;
}
#endif

int melee_web_source_file_request(int entry, uintptr_t offset,
                                  uintptr_t destination, size_t size,
                                  int type, int priority,
                                  MeleeWebSourceFileCallback callback,
                                  void* args, int* request_id,
                                  char* error, size_t error_size)
{
    const SourceFile* file = file_for_entry(entry);
    const size_t transfer_extent = file ? source_file_transfer_size(file) : 0;
    if (!file || !callback || !request_id || args != NULL ||
        (type != 0x21 && type != 0x23) ||
        priority != 1 || offset % 32 != 0 || size == 0 || size % 32 != 0 ||
        offset > transfer_extent ||
        size > transfer_extent - (size_t)offset ||
        (file->external && size > MELEE_WEB_SOURCE_FILE_STREAM_MAX) ||
        !destination_owned(destination, size)) {
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
        if(active_scope&&active_scope->recorded.status.armed)active_scope->recorded.status.failed=1;
#endif
        return fail(error, error_size,
                    "Unsupported or unowned source DVD/DevCom request");
    }
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if(active_scope->recorded.status.armed){
        const MeleeWebSourceFileRecordedCompletionPlan* p=&active_scope->recorded.plan;
        if(active_scope->recorded.status.failed||active_scope->recorded.status.request_seen||
           !active_scope->recorded.cursor_seen||active_scope->pumping||active_scope->requests||
           active_scope->request_count||!recorded_region_live(active_scope)||
           active_scope->recorded.status.last_preconsume_cursor!=p->request_cursor||
           entry!=p->entry||file!=active_scope->recorded.file||offset!=p->offset||
           destination!=p->destination||size!=p->transfer_size||type!=p->type||priority!=p->priority||
           !p->callback_matches(callback))
            return recorded_fail(active_scope,error,error_size,"Recorded request identity/cursor refused");
    }
#endif
    if (next_source_request < 0x50000000 ||
        next_source_request > INT_MAX - 4) {
        return fail(error, error_size,
                    "Source DVD/DevCom request identity is exhausted");
    }
    SourceFileRequest* pending = calloc(1, sizeof(*pending));
    if (!pending) {
        return fail(error, error_size,
                    "Unable to allocate source DVD/DevCom request");
    }
    pending->file = file;
    pending->offset = offset;
    pending->destination = destination;
    pending->size = size;
    pending->callback = callback;
    pending->id = next_source_request++;
    source_file_copy_forget_overlaps(active_scope, destination, size);
    *active_scope->request_tail = pending;
    active_scope->request_tail = &pending->next;
    ++active_scope->request_count;
    *request_id = pending->id;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if(active_scope->recorded.status.armed){
        active_scope->recorded.status.request_seen=1;
        active_scope->recorded.status.request_id=pending->id;
        active_scope->recorded.status.actual_request_cursor=active_scope->recorded.status.last_preconsume_cursor;
    }
#endif
    if (error && error_size) error[0] = '\0';
    return 1;
}

int melee_web_source_file_cancel(int request_id, uint32_t flags,
                                 MeleeWebSourceFileCallback callback,
                                 void* args)
{
    if (!active_scope || (flags & ~3u) || args != NULL) return 0;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if(active_scope->recorded.status.armed){active_scope->recorded.status.failed=1;return 0;}
#endif
    for (SourceFileRequest* request = active_scope->requests; request;
         request = request->next) {
        if (request->id != request_id) continue;
        if (flags & 1u) request->callback = callback;
        request->cancelled = 1;
#if defined(__EMSCRIPTEN__)
        if (request->file->external) source_stream_discard(request_id);
#endif
        return 1;
    }
    return 0;
}

int melee_web_source_file_supply(int request_id, const void* bytes, size_t size,
                                 char* error, size_t error_size)
{
    if (!active_scope || (!bytes && size))
        return fail(error, error_size,
                    "Invalid external source file transfer result");
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if(active_scope->recorded.status.armed&&!active_scope->recorded.committing)
        return recorded_fail(active_scope,error,error_size,"Recorded transfer supplied before eligible boundary");
#endif
    SourceFileRequest* request = active_scope->requests;
    while (request && request->id != request_id) request = request->next;
    if (!request || !request->file->external || request->cancelled ||
        request->stream_ready || size != request->size || !size ||
        !destination_owned(request->destination, request->size)) {
        return fail(error, error_size,
                    "External source file transfer has no matching owner");
    }
    source_file_copy_forget_overlaps(active_scope, request->destination,
                                     request->size);
    memcpy((void*)request->destination, bytes, size);
    request->stream_ready = 1;
    if (error && error_size) error[0] = '\0';
    return 1;
}

int melee_web_source_files_pump_for(MeleeWebSourceFileScope* scope,
                                    char* error, size_t error_size)
{
    if (!scope || active_scope != scope)
        return fail(error, error_size,
                    "Source RuntimeFiles pump lost its exact owner");
    if (scope->pumping) {
        return fail(error, error_size,
                    "Reentrant source RuntimeFiles transfer pump");
    }
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if(scope->recorded.status.armed&&!scope->recorded.committing){
        if(scope->recorded.status.failed)return fail(error,error_size,"Recorded source completion already failed");
        if(scope->recorded.status.completed)return scope->request_count==0?1:recorded_fail(scope,error,error_size,"Recorded completion has trailing request");
        return recorded_pending_matches(scope,error,error_size);
    }
#endif
    size_t available = scope->request_count;
    size_t budget = 64;
    scope->pumping = 1;
    while (available && budget-- && scope->requests) {
        SourceFileRequest* request = scope->requests;
        if (!request->cancelled && request->file->external &&
            !request->stream_ready) {
#if defined(__EMSCRIPTEN__)
            if (!request->stream_dispatched) {
                if (request->offset > UINT32_MAX || request->size > UINT32_MAX ||
                    !source_stream_start(request->id, request->file->name,
                                         (uint32_t)request->offset,
                                         (uint32_t)request->size)) {
                    scope->pumping = 0;
                    return fail(error, error_size,
                                "Browser source file stream service is unavailable");
                }
                request->stream_dispatched = 1;
            }
            int status = 0;
            const double deadline = emscripten_get_now() + 30000.0;
            while ((status = source_stream_status(request->id)) == 0 &&
                   emscripten_get_now() < deadline) {
                scope->pumping = 0;
                emscripten_sleep(1);
                if (active_scope != scope)
                    return fail(error, error_size,
                                "Source RuntimeFiles owner changed during streamed read");
                scope->pumping = 1;
            }
            if (status != 1) {
                scope->pumping = 0;
                source_stream_discard(request->id);
                return fail(error, error_size,
                            status < 0 ? "Original disc range read failed" :
                                         "Original disc range read timed out");
            }
            uint32_t staging = source_stream_take(request->id);
            if (!staging) {
                scope->pumping = 0;
                return fail(error, error_size,
                            "Original disc range result lost its owner");
            }
            char supply_error[128] = {0};
            if (!melee_web_source_file_supply(request->id,
                    (const void*)(uintptr_t)staging, request->size,
                    supply_error, sizeof(supply_error))) {
                free((void*)(uintptr_t)staging);
                scope->pumping = 0;
                return fail(error, error_size,
                            supply_error[0] ? supply_error :
                                "Original disc range transfer lost its owner");
            }
            free((void*)(uintptr_t)staging);
#else
            scope->pumping = 0;
            return fail(error, error_size,
                        "External source file streaming requires the browser runtime");
#endif
        }
        if (!request->cancelled) {
            /* A later synchronous copy may have been recorded after this
             * request was admitted or supplied. Invalidate its overlapping
             * receipt before this request's final bytes become authoritative. */
            source_file_copy_forget_overlaps(scope, request->destination,
                                             request->size);
            if (!request->file->external) {
                if (!source_file_copy_transfer(
                        request->file, request->offset,
                        (void*)request->destination, request->size)) {
                    scope->pumping = 0;
                    return fail(error, error_size,
                                "Authenticated source transfer extent changed before copy");
                }
            }
            if (request->offset == 0 &&
                request->size == source_file_transfer_size(request->file) &&
                request->size >= request->file->size) {
                SourceFileCopy* copy = source_file_copy_prepare(
                    scope, request->destination);
                if (!copy) {
                    scope->pumping = 0;
                    return fail(error, error_size,
                                "Unable to retain completed source file copy identity");
                }
                source_file_copy_commit(copy, request->file,
                                        request->size);
            }
        }
        scope->requests = request->next;
        if (!scope->requests) scope->request_tail = &scope->requests;
        --scope->request_count;
        --available;
        MeleeWebSourceFileCallback callback = request->callback;
        const int id = request->id;
        const int cancelled = request->cancelled;
        free(request);
        if (callback) callback(id, 0, NULL, cancelled != 0);
        if (active_scope != scope) {
            return fail(error, error_size,
                        "Source RuntimeFiles owner changed during completion");
        }
    }
    scope->pumping = 0;
    if (error && error_size) error[0] = '\0';
    return 1;
}

int melee_web_source_files_pump(char* error, size_t error_size)
{
    if (!active_scope) {
        if (error && error_size) error[0] = '\0';
        return 1;
    }
    return melee_web_source_files_pump_for(active_scope, error, error_size);
}

int melee_web_source_files_quiescent_for(MeleeWebSourceFileScope* scope,
                                         char* error, size_t error_size)
{
    if (!scope || active_scope != scope)
        return fail(error, error_size,
                    "Source RuntimeFiles teardown lost its exact owner");
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if(scope->recorded.status.armed&&(!scope->recorded.status.completed||scope->recorded.status.failed))
        return fail(error,error_size,"Recorded source completion has not completed its exact boundary");
#endif
    if (scope->pumping || scope->requests || scope->request_count)
        return fail(error, error_size,
                    "Source RuntimeFiles teardown has pending requests");
    if (error && error_size) error[0] = '\0';
    return 1;
}
