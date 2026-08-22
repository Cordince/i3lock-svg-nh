#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "scene_pack_lib.h"

static void usage(void) {
    fprintf(stderr,
            "Usage: scene-pack --input source.svg --output packed.svg [--asset-root PATH] [--force]\n"
            "                  [--check] [--print-assets] [--version] [--help]\n");
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *output = NULL;
    const char *asset_root_arg = NULL;
    bool force = false;
    bool check = false;
    bool print_assets = false;

    enum {
        OPT_INPUT = 256,
        OPT_OUTPUT,
        OPT_ASSET_ROOT,
        OPT_FORCE,
        OPT_CHECK,
        OPT_PRINT_ASSETS,
        OPT_VERSION
    };
    struct option longopts[] = {
        {"input", required_argument, NULL, OPT_INPUT},
        {"output", required_argument, NULL, OPT_OUTPUT},
        {"asset-root", required_argument, NULL, OPT_ASSET_ROOT},
        {"force", no_argument, NULL, OPT_FORCE},
        {"check", no_argument, NULL, OPT_CHECK},
        {"print-assets", no_argument, NULL, OPT_PRINT_ASSETS},
        {"version", no_argument, NULL, OPT_VERSION},
        {"help", no_argument, NULL, 'h'},
        {NULL, no_argument, NULL, 0}
    };

    int opt;
    int longindex = 0;
    while ((opt = getopt_long(argc, argv, "h", longopts, &longindex)) != -1) {
        switch (opt) {
            case OPT_INPUT:
                input = optarg;
                break;
            case OPT_OUTPUT:
                output = optarg;
                break;
            case OPT_ASSET_ROOT:
                asset_root_arg = optarg;
                break;
            case OPT_FORCE:
                force = true;
                break;
            case OPT_CHECK:
                check = true;
                break;
            case OPT_PRINT_ASSETS:
                print_assets = true;
                break;
            case OPT_VERSION:
                printf("scene-pack %s\n", VERSION);
                return EXIT_SUCCESS;
            case 'h':
                usage();
                return EXIT_SUCCESS;
            default:
                usage();
                return EXIT_FAILURE;
        }
    }
    if (!input || (!output && !check)) {
        usage();
        return EXIT_FAILURE;
    }

    char error[SCENE_ERROR_LEN] = "";
    scene_pack_result_t packed;
    scene_pack_result_init(&packed);
    if (check) {
        if (!scene_pack_source_to_memory(input, asset_root_arg, &packed, error))
            errx(EXIT_FAILURE, "%s", error);
        if (print_assets || check)
            scene_pack_print_assets(&packed.assets, stdout);
        printf("scene-pack check ok: %zu PNG asset(s)\n", packed.assets.count);
        scene_pack_result_clear(&packed);
        return EXIT_SUCCESS;
    }

    if (!scene_pack_source_to_file(input, output, asset_root_arg, force, &packed, error))
        errx(EXIT_FAILURE, "%s", error);
    if (print_assets)
        scene_pack_print_assets(&packed.assets, stdout);
    printf("scene-pack wrote %s with %zu PNG asset(s)\n", output, packed.assets.count);
    scene_pack_result_clear(&packed);
    return EXIT_SUCCESS;
}
