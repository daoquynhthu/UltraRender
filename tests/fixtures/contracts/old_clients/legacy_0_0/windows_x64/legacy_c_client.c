#include <windows.h>

#include <ure/ure_c_api.h>

typedef int(__cdecl* ure_aov_channel_count_fn)(ure_aov_type_t);

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
    HMODULE runtime = LoadLibraryA(argv[1]);
    if (runtime == NULL) {
        return 3;
    }
    ure_aov_channel_count_fn channel_count =
        (ure_aov_channel_count_fn)GetProcAddress(
            runtime, "ure_aov_channel_count");
    if (channel_count == NULL) {
        FreeLibrary(runtime);
        return 4;
    }
    const int result = channel_count(URE_AOV_BEAUTY) == 3 ? 0 : 5;
    FreeLibrary(runtime);
    return result;
}
