#ifndef MELEE_WEB_GAMEPLAY_ARCHIVE_SECTIONS_H
#define MELEE_WEB_GAMEPLAY_ARCHIVE_SECTIONS_H
#include <stddef.h>
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebArchiveSymbol {
    const char* filename;
    const char* symbol;
    /* NULL declares an actual source public name whose typed graph is not yet
     * hydrated. It permits archive ownership, but accessing that symbol fails
     * explicitly. Populate such names from a validated archive public table;
     * never use a placeholder symbol to make a missing archive look present. */
    void* native_data;
} MeleeWebArchiveSymbol;
typedef struct MeleeWebArchiveSections MeleeWebArchiveSections;
/* Copies names; borrows complete typed descriptor graphs. The caller must retain
 * those graphs until every source consumer finishes and this scope is removed. */
MeleeWebArchiveSections* melee_web_archive_sections_register(const MeleeWebArchiveSymbol*, size_t, char*, size_t);
/* Use only for archives originally allocated from/discarded with the scene
 * heap (e.g. lbCardGame_LoadArchive). Requires a live SDK world and exclusive
 * ownership of each filename. close rejects until that world is destroyed,
 * then releases any source handles discarded by those original callers. */
MeleeWebArchiveSections* melee_web_archive_sections_register_heap(const MeleeWebArchiveSymbol*, size_t, char*, size_t);
int melee_web_archive_sections_close(MeleeWebArchiveSections*, char*, size_t);
/* Atomically release the scope's one owned handle and close it. Any other
 * open consumer rejects without changing the handle or scope. */
int melee_web_archive_sections_close_owned(MeleeWebArchiveSections*,void* handle,char*,size_t);
/* Opaque source archive handles resolve only registered, owned typed symbols.
 * No archive bytes are reinterpreted or relocated in place. Every open must be
 * released before a scope containing that archive can close. Unknown handles
 * and unregistered filenames fail explicitly. A missing public name returns NULL
 * like the original HSD query; required-section loading fails atomically. */
void* melee_web_archive_sections_open(const char* filename);
void* melee_web_archive_sections_public(void* handle, const char* symbol);
void melee_web_archive_sections_release(void* handle);
/* Resolves every requested symbol before publishing any outputs. A non-NULL
 * archive_destination receives an owned opaque handle after validation. */
void melee_web_archive_sections_load(void* archive_destination, const char* filename,
    void* first_destination, va_list args);
#ifdef __cplusplus
}
#endif
#endif
