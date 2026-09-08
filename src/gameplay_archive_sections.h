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
    void* native_data;
} MeleeWebArchiveSymbol;
typedef struct MeleeWebArchiveSections MeleeWebArchiveSections;
/* Copies names; borrows complete typed descriptor graphs. The caller must retain
 * those graphs until every source consumer finishes and this scope is removed. */
MeleeWebArchiveSections* melee_web_archive_sections_register(const MeleeWebArchiveSymbol*, size_t, char*, size_t);
int melee_web_archive_sections_close(MeleeWebArchiveSections*, char*, size_t);
/* Source storage adapter. Resolves every requested symbol before publishing any
 * outputs. Raw archive-handle requests are unsupported and fail explicitly. */
void melee_web_archive_sections_load(void* archive_destination, const char* filename,
    void* first_destination, va_list args);
#ifdef __cplusplus
}
#endif
#endif
