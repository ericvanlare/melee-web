#include "gameplay_source_files.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const uint8_t bytes[] = {0x48, 0x53, 0x44, 0x00, 0x31, 0x32};
    char name[] = "LbRf.dat";
    const MeleeWebSourceFileInput input[] = {
        {name, bytes, sizeof(bytes)},
        {"zero-length.bin", NULL, 0},
    };
    char error[128];
    size_t size = 0;

    assert(!melee_web_source_file_size("LbRf.dat", &size));
    MeleeWebSourceFileScope* scope = melee_web_source_files_begin(
        input, sizeof(input) / sizeof(input[0]), error, sizeof(error));
    assert(scope && error[0] == '\0');
    name[0] = 'X'; /* The exact lookup index owns its names. */

    assert(melee_web_source_file_size("LbRf.dat", &size));
    assert(size == sizeof(bytes));
    assert(melee_web_source_file_size("/LbRf.dat", &size));
    assert(size == sizeof(bytes));
    uint8_t copy[sizeof(bytes)] = {0};
    assert(melee_web_source_file_copy("/LbRf.dat", copy, &size));
    assert(size == sizeof(bytes) && memcmp(copy, bytes, sizeof(bytes)) == 0);
    assert(strcmp(melee_web_source_file_take_name(copy), "LbRf.dat") == 0);
    assert(melee_web_source_file_take_name(copy) == NULL);
    assert(melee_web_source_file_size("zero-length.bin", &size) && size == 0);
    assert(!melee_web_source_file_size("lbRf.dat", &size));
    assert(!melee_web_source_file_size("missing.dat", &size));
    assert(!melee_web_source_file_size("//LbRf.dat", &size));
    assert(!melee_web_source_file_size("/lbrf.dat", &size));
    assert(!melee_web_source_file_size("/missing/LbRf.dat", &size));
    assert(!melee_web_source_file_copy("LbRf.dat", NULL, &size));

    assert(!melee_web_source_files_begin(input, 2, error, sizeof(error)));
    assert(strstr(error, "already active") != NULL);
    assert(melee_web_source_files_end(scope, error, sizeof(error)));
    assert(!melee_web_source_file_size("LbRf.dat", &size));
    assert(!melee_web_source_files_end(scope, error, sizeof(error)));

    const MeleeWebSourceFileInput duplicate[] = {
        {"same.dat", bytes, sizeof(bytes)},
        {"same.dat", bytes, sizeof(bytes)},
    };
    assert(!melee_web_source_files_begin(duplicate, 2, error, sizeof(error)));
    assert(strstr(error, "Duplicate exact") != NULL);
    puts("source RuntimeFiles DVD-root-path, exact-name, and lifecycle trace: passed");
    return 0;
}
