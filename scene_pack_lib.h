#ifndef _SCENE_PACK_LIB_H
#define _SCENE_PACK_LIB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "scene.h"

typedef struct {
    char *source_href;
    char *canonical_path;
    size_t byte_size;
    bool embedded;
} scene_pack_asset_t;

typedef struct {
    scene_pack_asset_t *items;
    size_t count;
    size_t capacity;
} scene_pack_asset_list_t;

typedef struct {
    char *data;
    size_t len;
    scene_pack_asset_list_t assets;
    char *canonical_input;
    char *canonical_source_dir;
    char *canonical_asset_root;
} scene_pack_result_t;

void scene_pack_result_init(scene_pack_result_t *result);
void scene_pack_result_clear(scene_pack_result_t *result);

bool scene_pack_source_to_memory(const char *input,
                                 const char *asset_root_arg,
                                 scene_pack_result_t *out,
                                 char error[SCENE_ERROR_LEN]);

bool scene_pack_source_to_file(const char *input,
                               const char *output,
                               const char *asset_root_arg,
                               bool force,
                               scene_pack_result_t *out_info,
                               char error[SCENE_ERROR_LEN]);

bool scene_pack_validate_memory(const char *display_path,
                                const char *data,
                                size_t len,
                                char error[SCENE_ERROR_LEN]);

bool scene_pack_print_assets(const scene_pack_asset_list_t *assets, FILE *out);

#endif
