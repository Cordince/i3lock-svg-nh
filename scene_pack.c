#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <glib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "scene_renderer.h"

typedef struct {
    char *source_href;
    char *canonical_path;
    size_t byte_size;
    bool embedded;
} packed_asset_t;

typedef struct {
    packed_asset_t *items;
    size_t count;
    size_t capacity;
} asset_list_t;

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

static bool add_asset(asset_list_t *list, const char *href, const char *canonical, size_t byte_size, bool embedded) {
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2 : 8;
        packed_asset_t *items = realloc(list->items, next * sizeof(*items));
        if (!items)
            return false;
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count].source_href = strdup(href);
    list->items[list->count].canonical_path = canonical ? strdup(canonical) : strdup("(embedded)");
    list->items[list->count].byte_size = byte_size;
    list->items[list->count].embedded = embedded;
    if (!list->items[list->count].source_href || !list->items[list->count].canonical_path)
        return false;
    list->count++;
    return true;
}

static void free_assets(asset_list_t *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].source_href);
        free(list->items[i].canonical_path);
    }
    free(list->items);
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

static char *resolve_asset_path(const char *href, const char *source_dir, const char *asset_root, char error[SCENE_ERROR_LEN]) {
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
                             asset_list_t *assets,
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
                bool remove_absref = g_ascii_strcasecmp(attr_name, "absref") == 0;
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

static void usage(void) {
    fprintf(stderr,
            "Usage: scene-pack --input source.svg --output packed.svg [--asset-root PATH] [--force]\n"
            "                  [--check] [--print-assets]\n");
}

static bool validate_document_through_runtime(xmlDocPtr doc, char error[SCENE_ERROR_LEN]) {
    gchar *tmp_path = NULL;
    GError *gerr = NULL;
    int fd = g_file_open_tmp("i3lock-scene-pack-check-XXXXXX.svg", &tmp_path, &gerr);
    if (fd < 0) {
        snprintf(error, SCENE_ERROR_LEN, "could not create temporary validation file: %s",
                 gerr ? gerr->message : "unknown error");
        if (gerr)
            g_error_free(gerr);
        return false;
    }
    close(fd);
    if (xmlSaveFormatFileEnc(tmp_path, doc, "UTF-8", 0) < 0) {
        snprintf(error, SCENE_ERROR_LEN, "could not write temporary validation SVG");
        unlink(tmp_path);
        g_free(tmp_path);
        return false;
    }
    bool ok = scene_renderer_validate_file(tmp_path, error);
    unlink(tmp_path);
    g_free(tmp_path);
    return ok;
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
        OPT_PRINT_ASSETS
    };
    struct option longopts[] = {
        {"input", required_argument, NULL, OPT_INPUT},
        {"output", required_argument, NULL, OPT_OUTPUT},
        {"asset-root", required_argument, NULL, OPT_ASSET_ROOT},
        {"force", no_argument, NULL, OPT_FORCE},
        {"check", no_argument, NULL, OPT_CHECK},
        {"print-assets", no_argument, NULL, OPT_PRINT_ASSETS},
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
    if (output && !force && access(output, F_OK) == 0)
        errx(EXIT_FAILURE, "output '%s' already exists; use --force to overwrite", output);

    char error[SCENE_ERROR_LEN] = "";
    char *canonical_input = realpath(input, NULL);
    if (!canonical_input)
        err(EXIT_FAILURE, "could not resolve input '%s'", input);
    char *source_dir = path_dirname(canonical_input);
    char *canonical_source_dir = realpath(source_dir, NULL);
    if (!canonical_source_dir)
        err(EXIT_FAILURE, "could not resolve source or asset root");
    char *canonical_asset_root = asset_root_arg ? realpath(asset_root_arg, NULL) : strdup(canonical_source_dir);
    if (!canonical_asset_root)
        err(EXIT_FAILURE, "could not resolve source or asset root");

    xmlDocPtr doc = xmlReadFile(canonical_input, NULL, XML_PARSE_NONET);
    if (!doc)
        errx(EXIT_FAILURE, "could not parse SVG '%s'", input);
    if (doc->intSubset || doc->extSubset)
        errx(EXIT_FAILURE, "DOCTYPE declarations are not allowed");

    asset_list_t assets = {0};
    if (!process_document(doc, xmlDocGetRootElement(doc), canonical_source_dir, canonical_asset_root, &assets, error))
        errx(EXIT_FAILURE, "%s", error);

    if (print_assets || check) {
        for (size_t i = 0; i < assets.count; i++) {
            printf("%s -> %s bytes=%zu mime=image/png\n",
                   assets.items[i].source_href,
                   assets.items[i].canonical_path,
                   assets.items[i].byte_size);
        }
    }
    if (check) {
        if (!validate_document_through_runtime(doc, error))
            errx(EXIT_FAILURE, "packed output would be invalid: %s", error);
        printf("scene-pack check ok: %zu PNG asset(s)\n", assets.count);
        free_assets(&assets);
        xmlFreeDoc(doc);
        free(canonical_asset_root);
        free(canonical_source_dir);
        free(source_dir);
        free(canonical_input);
        return EXIT_SUCCESS;
    }

    char tmp_path[PATH_MAX];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", output, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(tmp_path))
        errx(EXIT_FAILURE, "output path too long");
    if (xmlSaveFormatFileEnc(tmp_path, doc, "UTF-8", 0) < 0)
        errx(EXIT_FAILURE, "could not write temporary output '%s'", tmp_path);
    if (!scene_renderer_validate_file(tmp_path, error)) {
        unlink(tmp_path);
        errx(EXIT_FAILURE, "packed output is invalid: %s", error);
    }
    if (rename(tmp_path, output) != 0) {
        unlink(tmp_path);
        err(EXIT_FAILURE, "could not install output '%s'", output);
    }
    printf("scene-pack wrote %s with %zu PNG asset(s)\n", output, assets.count);

    free_assets(&assets);
    xmlFreeDoc(doc);
    free(canonical_asset_root);
    free(canonical_source_dir);
    free(source_dir);
    free(canonical_input);
    return EXIT_SUCCESS;
}
