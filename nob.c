#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "./extern/nob.h"

#define SOURCE_FOLDER "./src/"
#define BUILD_FOLDER "./build/"

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (!mkdir_if_not_exists(BUILD_FOLDER)) return 1;
    Cmd cmd = {0};

    if (needs_rebuild1(BUILD_FOLDER"cerdeb", SOURCE_FOLDER"main.c")) {
        nob_cc(&cmd);
        nob_cc_flags(&cmd);
        cmd_append(&cmd, "-ggdb");
        nob_cc_inputs(&cmd, SOURCE_FOLDER"main.c");
        nob_cc_output(&cmd, BUILD_FOLDER"cerdeb");
        if (!cmd_run(&cmd)) return 1;
    } else {
        nob_log(NOB_INFO, "Up to date");
    }

    cmd_append(&cmd, BUILD_FOLDER"cerdeb");
    if (!cmd_run(&cmd)) return 1;

    return 0;
}
