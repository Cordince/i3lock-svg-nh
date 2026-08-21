#include "scene_pack_lib.h"

#include <errno.h>
#include <glib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "scene_renderer.h"

static bool starts_with_case(const char *value, const char *prefix) {
    return g_ascii_strncasecmp(value, prefix, strlen(prefix)) == 0;
}

static bool is_png_path(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot && g_ascii_strcasecmp(dot, ".png") == 0;
}

static bool is_network_uri(const char *value) {
    return starts_with_case(value, "http://") ||
           starts_with_case(value, "https://") ||
           starts_with_case(value, "ftp://");
}

static bool is_data_png(const char *value) {
    return starts_with_case(value, "data:image/png;base64,");
}

static bool set_error(char error[SCENE_ERROR_LEN], const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(error, SCENE_ERROR_LEN, fmt, args);
    va_end(args);
    return false;
}

static bool add_asset(scene_pack_asset_list_t *list,
                      const char *href,
                      const char *canonical,
                      size_t byte_size,
                      bool embedded) {
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2 : 8;
        scene_pack_asset_t *items = realloc(list->items, next * sizeof(*items));
        if (!items)
            return false;
        list->items = items;
        list->capacity = next;
    }
    scene_pack_asset_t *asset = &list->items[list->count];
    asset->source_href = strdup(href);
    asset->canonical_path = canonical ? strdup(canonical) : strdup("(embedded)");
    asset->byte_size = byte_size;
    asset->embedded = embedded;
    if (!asset->source_href || !asset->canonical_path) {
        free(asset->source_href);
        free(asset->canonical_path);
        memset(asset, 0, sizeof(*asset));
        return false;
    }
    list->count++;
    return true;
}

static void clear_assets(scene_pack_asset_list_t *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].source_href);
        free(list->items[i].canonical_path);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

void scene_pack_result_init(scene_pack_result_t *result) {
    memset(result, 0, sizeof(*result));
}

void scene_pack_result_clear(scene_pack_result_t *result) {
    if (!result)
        return;
    free(result->data);
    clear_assets(&result->assets);
    free(result->canonical_input);
    free(result->canonical_source_dir);
    free(result->canonical_asset_root);
    memset(result, 0, sizeof(*result));
}

static char *path_dirname(const char *path) {
    char *copy = strdup(path);
    if (!copy)
        return NULL;
    char *slash = strrchr(copy, '/');
    if (!slash) {
        strcpy(copy, ".");
        return copy;
    }
    if (slash == copy) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    return copy;
}

static bool path_inside_root(const char *path, const char *root) {
    size_t len = strlen(root);
    return strncmp(path, root, len) == 0 && (path[len] == '\0' || path[len] == '/');
}

static char *resolve_asset_path(const char *href,
                                const char *source_dir,
                                const char *asset_root,
                                char error[SCENE_ERROR_LEN]) {
    if (href[0] == '/' || strstr(href, "://") || starts_with_case(href, "file:")) {
        snprintf(error, SCENE_ERROR_LEN, "unsafe image path '%s'", href);
        return NULL;
    }
    char joined[PATH_MAX];
    int n = snprintf(joined, sizeof(joined), "%s/%s", source_dir, href);
    if (n < 0 || (size_t)n >= sizeof(joined)) {
        snprintf(error, SCENE_ERROR_LEN, "asset path is too long");
        return NULL;
    }
    char *canonical = realpath(joined, NULL);
    if (!canonical) {
        snprintf(error, SCENE_ERROR_LEN, "could not resolve asset '%s': %s", href, strerror(errno));
        return NULL;
    }
    if (!path_inside_root(canonical, asset_root)) {
        snprintf(error, SCENE_ERROR_LEN, "asset '%s' escapes asset root", href);
        free(canonical);
        return NULL;
    }
    if (!is_png_path(canonical)) {
        snprintf(error, SCENE_ERROR_LEN, "scene-v1 packer only embeds PNG images: '%s'", href);
        free(canonical);
        return NULL;
    }
    return canonical;
}

static bool validate_style_text(const char *text, char error[SCENE_ERROR_LEN]) {
    if (strcasestr(text, "@import")) {
        snprintf(error, SCENE_ERROR_LEN, "CSS @import is not allowed");
        return false;
    }
    const char *p = text;
    while ((p = strcasestr(p, "url(")) != NULL) {
        p += 4;
        while (*p == ' ' || *p == '\t' || *p == '\'' || *p == '"')
            p++;
        if (*p != '#') {
            snprintf(error, SCENE_ERROR_LEN, "external CSS url(...) is not packable");
            return false;
        }
    }
    return true;
}

static const char *node_name(xmlNodePtr node) {
    return node && node->name ? (const char *)node->name : "unknown";
}

static bool process_document(xmlDocPtr doc,
                             xmlNodePtr node,
                             const char *source_dir,
                             const char *asset_root,
                             scene_pack_asset_list_t *assets,
                             char error[SCENE_ERROR_LEN]) {
    for (xmlNodePtr cur = node; cur; cur = cur->next) {
        if (cur->type == XML_DTD_NODE) {
            snprintf(error, SCENE_ERROR_LEN, "DOCTYPE declarations are not allowed");
            return false;
        }
        if (cur->type == XML_PI_NODE && g_ascii_strcasecmp(node_name(cur), "xml-stylesheet") == 0) {
            snprintf(error, SCENE_ERROR_LEN, "XML stylesheet processing instructions are not allowed");
            return false;
        }
        if (cur->type == XML_TEXT_NODE && cur->parent && cur->parent->name &&
            g_ascii_strcasecmp((const char *)cur->parent->name, "style") == 0) {
            if (!validate_style_text((const char *)cur->content, error))
                return false;
        }
        if (cur->type == XML_ELEMENT_NODE) {
            const char *name = node_name(cur);
            if (g_ascii_strcasecmp(name, "include") == 0 && cur->ns && cur->ns->href &&
                strcmp((const char *)cur->ns->href, "http://www.w3.org/2001/XInclude") == 0) {
                snprintf(error, SCENE_ERROR_LEN, "XML includes are not packable");
                return false;
            }
            for (xmlAttrPtr attr = cur->properties; attr;) {
                xmlAttrPtr next = attr->next;
                const char *attr_name = (const char *)attr->name;
                bool is_href = g_ascii_strcasecmp(attr_name, "href") == 0;
                bool remove_absref = g_ascii_strcasecmp(attr_name, "absref") == 0 ||
                                     g_ascii_strcasecmp(attr_name, "sodipodi:absref") == 0;
                xmlChar *value_xml = xmlNodeListGetString(doc, attr->children, 1);
                const char *value = value_xml ? (const char *)value_xml : "";
                if (remove_absref) {
                    xmlRemoveProp(attr);
                    if (value_xml)
                        xmlFree(value_xml);
                    attr = next;
                    continue;
                }
                if (g_ascii_strcasecmp(attr_name, "style") == 0) {
                    if (!validate_style_text(value, error)) {
                        if (value_xml)
                            xmlFree(value_xml);
                        return false;
                    }
                }
                if (is_href) {
                    if (value[0] == '#') {
                        /* internal reference */
                    } else if (g_ascii_strcasecmp(name, "image") == 0) {
                        if (is_data_png(value)) {
                            if (!add_asset(assets, value, NULL, strlen(value), true)) {
                                if (value_xml)
                                    xmlFree(value_xml);
                                return false;
                            }
                        } else if (is_network_uri(value) || starts_with_case(value, "file:") || value[0] == '/') {
                            snprintf(error, SCENE_ERROR_LEN, "unsafe image href '%s'", value);
                            if (value_xml)
                                xmlFree(value_xml);
                            return false;
                        } else {
                            char *canonical = resolve_asset_path(value, source_dir, asset_root, error);
                            if (!canonical) {
                                if (value_xml)
                                    xmlFree(value_xml);
                                return false;
                            }
                            gchar *contents = NULL;
                            gsize len = 0;
                            GError *gerr = NULL;
                            if (!g_file_get_contents(canonical, &contents, &len, &gerr)) {
                                snprintf(error, SCENE_ERROR_LEN, "could not read PNG '%s': %s",
                                         canonical, gerr ? gerr->message : "unknown error");
                                if (gerr)
                                    g_error_free(gerr);
                                free(canonical);
                                if (value_xml)
                                    xmlFree(value_xml);
                                return false;
                            }
                            gchar *encoded = g_base64_encode((const guchar *)contents, len);
                            gchar *uri = g_strdup_printf("data:image/png;base64,%s", encoded);
                            if (attr->ns)
                                xmlSetNsProp(cur, attr->ns, attr->name, (const xmlChar *)uri);
                            else
                                xmlSetProp(cur, attr->name, (const xmlChar *)uri);
                            if (!add_asset(assets, value, canonical, len, true)) {
                                g_free(uri);
                                g_free(encoded);
                                g_free(contents);
                                free(canonical);
                                if (value_xml)
                                    xmlFree(value_xml);
                                return false;
                            }
                            g_free(uri);
                            g_free(encoded);
                            g_free(contents);
                            free(canonical);
                        }
                    } else {
                        snprintf(error, SCENE_ERROR_LEN, "external <%s> href '%s' is not packable", name, value);
                        if (value_xml)
                            xmlFree(value_xml);
                        return false;
                    }
                } else if (strstr(value, "url(") || strstr(value, "URL(")) {
                    if (!validate_style_text(value, error)) {
                        if (value_xml)
                            xmlFree(value_xml);
                        return false;
                    }
                }
                if (value_xml)
                    xmlFree(value_xml);
                attr = next;
            }
        }
        if (cur->children && !process_document(doc, cur->children, source_dir, asset_root, assets, error))
            return false;
    }
    return true;
}

bool scene_pack_validate_memory(const char *display_path,
                                const char *data,
                                size_t len,
                                char error[SCENE_ERROR_LEN]) {
    scene_renderer_t *renderer = NULL;
    scene_document_t *doc = NULL;
    if (!scene_document_load_from_data(display_path, data, len, &doc, error))
        return false;
    uint32_t width = (uint32_t)ceil(doc->width);
    uint32_t height = (uint32_t)ceil(doc->height);
    scene_document_free(doc);
    if (!scene_renderer_create_headless_from_data(display_path, data, len, width, height, &renderer, error))
        return false;
    scene_renderer_destroy(renderer);
    return true;
}

bool scene_pack_source_to_memory(const char *input,
                                 const char *asset_root_arg,
                                 scene_pack_result_t *out,
                                 char error[SCENE_ERROR_LEN]) {
    scene_pack_result_clear(out);
    char *canonical_input = realpath(input, NULL);
    if (!canonical_input)
        return set_error(error, "could not resolve input '%s': %s", input, strerror(errno));
    char *source_dir = path_dirname(canonical_input);
    char *canonical_source_dir = source_dir ? realpath(source_dir, NULL) : NULL;
    if (!canonical_source_dir) {
        free(canonical_input);
        free(source_dir);
        return set_error(error, "could not resolve source directory");
    }
    char *canonical_asset_root = asset_root_arg ? realpath(asset_root_arg, NULL) : strdup(canonical_source_dir);
    if (!canonical_asset_root) {
        free(canonical_input);
        free(source_dir);
        free(canonical_source_dir);
        return set_error(error, "could not resolve asset root");
    }

    xmlDocPtr doc = xmlReadFile(canonical_input, NULL, XML_PARSE_NONET);
    if (!doc) {
        free(canonical_input);
        free(source_dir);
        free(canonical_source_dir);
        free(canonical_asset_root);
        return set_error(error, "could not parse SVG '%s'", input);
    }
    if (doc->intSubset || doc->extSubset) {
        xmlFreeDoc(doc);
        free(canonical_input);
        free(source_dir);
        free(canonical_source_dir);
        free(canonical_asset_root);
        return set_error(error, "DOCTYPE declarations are not allowed");
    }

    scene_pack_asset_list_t assets = {0};
    if (!process_document(doc, xmlDocGetRootElement(doc), canonical_source_dir, canonical_asset_root, &assets, error)) {
        clear_assets(&assets);
        xmlFreeDoc(doc);
        free(canonical_input);
        free(source_dir);
        free(canonical_source_dir);
        free(canonical_asset_root);
        return false;
    }

    xmlChar *buffer = NULL;
    int len = 0;
    xmlDocDumpMemoryEnc(doc, &buffer, &len, "UTF-8");
    xmlFreeDoc(doc);
    if (!buffer || len <= 0) {
        if (buffer)
            xmlFree(buffer);
        clear_assets(&assets);
        free(canonical_input);
        free(source_dir);
        free(canonical_source_dir);
        free(canonical_asset_root);
        return set_error(error, "could not serialize packed SVG");
    }

    char *data = malloc((size_t)len + 1);
    if (!data) {
        xmlFree(buffer);
        clear_assets(&assets);
        free(canonical_input);
        free(source_dir);
        free(canonical_source_dir);
        free(canonical_asset_root);
        return set_error(error, "out of memory");
    }
    memcpy(data, buffer, (size_t)len);
    data[len] = '\0';
    xmlFree(buffer);

    if (!scene_pack_validate_memory(input, data, (size_t)len, error)) {
        free(data);
        clear_assets(&assets);
        free(canonical_input);
        free(source_dir);
        free(canonical_source_dir);
        free(canonical_asset_root);
        return false;
    }

    out->data = data;
    out->len = (size_t)len;
    out->assets = assets;
    out->canonical_input = canonical_input;
    out->canonical_source_dir = canonical_source_dir;
    out->canonical_asset_root = canonical_asset_root;
    free(source_dir);
    return true;
}

bool scene_pack_source_to_file(const char *input,
                               const char *output,
                               const char *asset_root_arg,
                               bool force,
                               scene_pack_result_t *out_info,
                               char error[SCENE_ERROR_LEN]) {
    if (!force && access(output, F_OK) == 0)
        return set_error(error, "output '%s' already exists; use --force to overwrite", output);

    scene_pack_result_t packed;
    scene_pack_result_init(&packed);
    if (!scene_pack_source_to_memory(input, asset_root_arg, &packed, error))
        return false;

    char tmp_path[PATH_MAX];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", output, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(tmp_path)) {
        scene_pack_result_clear(&packed);
        return set_error(error, "output path too long");
    }
    FILE *tmp = fopen(tmp_path, "wb");
    if (!tmp) {
        scene_pack_result_clear(&packed);
        return set_error(error, "could not write temporary output '%s': %s", tmp_path, strerror(errno));
    }
    bool ok = fwrite(packed.data, 1, packed.len, tmp) == packed.len;
    if (fclose(tmp) != 0)
        ok = false;
    if (!ok) {
        unlink(tmp_path);
        scene_pack_result_clear(&packed);
        return set_error(error, "could not write temporary output '%s'", tmp_path);
    }
    if (!scene_renderer_validate_file(tmp_path, error)) {
        unlink(tmp_path);
        scene_pack_result_clear(&packed);
        return false;
    }
    if (rename(tmp_path, output) != 0) {
        unlink(tmp_path);
        scene_pack_result_clear(&packed);
        return set_error(error, "could not install output '%s': %s", output, strerror(errno));
    }

    if (out_info) {
        *out_info = packed;
    } else {
        scene_pack_result_clear(&packed);
    }
    return true;
}

bool scene_pack_print_assets(const scene_pack_asset_list_t *assets, FILE *out) {
    for (size_t i = 0; i < assets->count; i++) {
        fprintf(out,
                "%s -> %s bytes=%zu mime=image/png\n",
                assets->items[i].source_href,
                assets->items[i].canonical_path,
                assets->items[i].byte_size);
    }
    return ferror(out) == 0;
}
