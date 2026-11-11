#include "scene.h"

#include <ctype.h>
#include <errno.h>
#include <glib.h>
#include <inttypes.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCENE_NS "urn:i3lock-svg-nh:scene:1"

typedef struct {
    scene_layer_style_t style;
    bool actual_visible;
    bool desired_visible;
    bool transition_active;
    int64_t transition_start_ms;
    int transition_duration_ms;
    scene_layer_style_t transition_from;
    scene_layer_style_t transition_to;
    bool transition_end_visible;
    bool event_active;
    int64_t event_start_ms;
    int64_t visible_since_ms;
} scene_layer_runtime_t;

struct scene_runtime {
    const scene_document_t *doc;
    scene_state_t state;
    scene_layer_runtime_t *layers;
    bool animating;
};

typedef struct {
    scene_document_t *doc;
    char error[SCENE_ERROR_LEN];
    bool failed;
    bool saw_scene;
    bool in_metadata;
    bool in_scene;
    bool in_timeline;
    int depth;
    int metadata_depth;
    int scene_depth;
    ssize_t current_layer;
    char **groups;
    size_t group_count;
    int group_order;
} scene_parse_ctx_t;

static bool set_error(char error[SCENE_ERROR_LEN], const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(error, SCENE_ERROR_LEN, fmt, args);
    va_end(args);
    return false;
}

static void parse_fail(scene_parse_ctx_t *ctx, const char *fmt, ...) {
    if (ctx->failed)
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(ctx->error, sizeof(ctx->error), fmt, args);
    va_end(args);
    ctx->failed = true;
}

static const char *local_name(const char *name) {
    const char *colon = strrchr(name, ':');
    return colon ? colon + 1 : name;
}

static const char *attr_value(const char **names, const char **values, const char *key) {
    for (int i = 0; names && names[i]; i++) {
        if (strcmp(names[i], key) == 0 || strcmp(local_name(names[i]), key) == 0)
            return values[i];
    }
    return NULL;
}

static bool parse_double_full(const char *value, double *out) {
    if (!value || !*value)
        return false;
    errno = 0;
    char *end = NULL;
    double parsed = strtod(value, &end);
    if (errno != 0 || end == value || !isfinite(parsed))
        return false;
    while (*end && isspace((unsigned char)*end))
        end++;
    if (*end != '\0')
        return false;
    *out = parsed;
    return true;
}

static bool parse_svg_dimension(const char *value, double *out) {
    if (!value || !*value)
        return false;
    errno = 0;
    char *end = NULL;
    double parsed = strtod(value, &end);
    if (errno != 0 || end == value || !isfinite(parsed) || parsed <= 0 || parsed > 16384.0)
        return false;
    while (*end && isspace((unsigned char)*end))
        end++;
    if (strcmp(end, "px") == 0 || *end == '\0') {
        *out = parsed;
        return true;
    }
    return false;
}

static bool parse_int_positive(const char *value, int *out) {
    double d;
    if (!parse_double_full(value, &d) || d <= 0 || floor(d) != d || d > 2147483647.0)
        return false;
    *out = (int)d;
    return true;
}

static bool parse_bool_attr(const char *value, bool *out) {
    if (!value) {
        *out = false;
        return true;
    }
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_fit(const char *value, scene_fit_t *out) {
    if (!value || strcmp(value, "pixel") == 0) {
        *out = SCENE_FIT_PIXEL;
        return true;
    }
    if (strcmp(value, "contain") == 0) {
        *out = SCENE_FIT_CONTAIN;
        return true;
    }
    if (strcmp(value, "cover") == 0) {
        *out = SCENE_FIT_COVER;
        return true;
    }
    if (strcmp(value, "stretch") == 0) {
        *out = SCENE_FIT_STRETCH;
        return true;
    }
    return false;
}

static bool parse_easing(const char *value, scene_easing_t *out) {
    if (!value || strcmp(value, "linear") == 0) {
        *out = SCENE_EASE_LINEAR;
        return true;
    }
    if (strcmp(value, "ease-in") == 0) {
        *out = SCENE_EASE_IN;
        return true;
    }
    if (strcmp(value, "ease-out") == 0) {
        *out = SCENE_EASE_OUT;
        return true;
    }
    if (strcmp(value, "ease-in-out") == 0) {
        *out = SCENE_EASE_IN_OUT;
        return true;
    }
    return false;
}

static bool parse_trigger(const char *value, scene_trigger_t *out) {
    if (!value) {
        *out = SCENE_TRIGGER_NONE;
        return true;
    }
    if (strcmp(value, "key") == 0) {
        *out = SCENE_TRIGGER_KEY;
        return true;
    }
    if (strcmp(value, "backspace") == 0) {
        *out = SCENE_TRIGGER_BACKSPACE;
        return true;
    }
    return false;
}

static bool parse_blend(const char *value, scene_blend_t *out) {
    if (!value || strcmp(value, "normal") == 0) {
        *out = SCENE_BLEND_NORMAL;
        return true;
    }
    if (strcmp(value, "add") == 0) {
        *out = SCENE_BLEND_ADD;
        return true;
    }
    if (strcmp(value, "multiply") == 0) {
        *out = SCENE_BLEND_MULTIPLY;
        return true;
    }
    if (strcmp(value, "screen") == 0) {
        *out = SCENE_BLEND_SCREEN;
        return true;
    }
    if (strcmp(value, "overlay") == 0) {
        *out = SCENE_BLEND_OVERLAY;
        return true;
    }
    if (strcmp(value, "darken") == 0) {
        *out = SCENE_BLEND_DARKEN;
        return true;
    }
    if (strcmp(value, "lighten") == 0) {
        *out = SCENE_BLEND_LIGHTEN;
        return true;
    }
    if (strcmp(value, "difference") == 0) {
        *out = SCENE_BLEND_DIFFERENCE;
        return true;
    }
    if (strcmp(value, "exclusion") == 0) {
        *out = SCENE_BLEND_EXCLUSION;
        return true;
    }
    return false;
}

static bool parse_state_token(const char *token, uint32_t *mask) {
    if (strcmp(token, "locked") == 0) {
        *mask |= SCENE_STATE_MASK_LOCKED;
        return true;
    }
    if (strcmp(token, "input") == 0) {
        *mask |= SCENE_STATE_MASK_INPUT;
        return true;
    }
    if (strcmp(token, "verify") == 0) {
        *mask |= SCENE_STATE_MASK_VERIFY;
        return true;
    }
    if (strcmp(token, "fail") == 0) {
        *mask |= SCENE_STATE_MASK_FAIL;
        return true;
    }
    return false;
}

static bool parse_states(const char *value, uint32_t *out) {
    if (!value)
        return false;
    if (strcmp(value, "*") == 0) {
        *out = SCENE_STATE_MASK_ALL;
        return true;
    }

    char *copy = strdup(value);
    if (!copy)
        return false;
    uint32_t mask = 0;
    char *saveptr = NULL;
    for (char *tok = strtok_r(copy, " \t\r\n,", &saveptr);
         tok;
         tok = strtok_r(NULL, " \t\r\n,", &saveptr)) {
        if (!parse_state_token(tok, &mask)) {
            free(copy);
            return false;
        }
    }
    free(copy);
    if (mask == 0)
        return false;
    *out = mask;
    return true;
}

static bool parse_hex_background(const char *value,
                                 double *r,
                                 double *g,
                                 double *b,
                                 double *a) {
    if (!value) {
        *r = *g = *b = 0.0;
        *a = 1.0;
        return true;
    }
    if (value[0] == '#')
        value++;
    size_t len = strlen(value);
    if (len != 6 && len != 8)
        return false;
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)value[i]))
            return false;
    }
    char buf[3] = {0};
    buf[0] = value[0];
    buf[1] = value[1];
    *r = strtol(buf, NULL, 16) / 255.0;
    buf[0] = value[2];
    buf[1] = value[3];
    *g = strtol(buf, NULL, 16) / 255.0;
    buf[0] = value[4];
    buf[1] = value[5];
    *b = strtol(buf, NULL, 16) / 255.0;
    if (len == 8) {
        buf[0] = value[6];
        buf[1] = value[7];
        *a = strtol(buf, NULL, 16) / 255.0;
    } else {
        *a = 1.0;
    }
    return true;
}

static bool parse_viewbox(const char *value, scene_document_t *doc) {
    if (!value)
        return true;
    double vals[4];
    const char *p = value;
    for (int i = 0; i < 4; i++) {
        while (*p && (isspace((unsigned char)*p) || *p == ','))
            p++;
        errno = 0;
        char *end = NULL;
        vals[i] = strtod(p, &end);
        if (errno != 0 || end == p || !isfinite(vals[i]))
            return false;
        p = end;
    }
    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p != '\0' || vals[2] <= 0 || vals[3] <= 0)
        return false;
    doc->viewbox_x = vals[0];
    doc->viewbox_y = vals[1];
    doc->viewbox_width = vals[2];
    doc->viewbox_height = vals[3];
    doc->has_viewbox = true;
    return true;
}

static bool append_layer(scene_document_t *doc, scene_layer_t *layer) {
    scene_layer_t *new_layers = realloc(doc->layers, sizeof(scene_layer_t) * (doc->layer_count + 1));
    if (!new_layers)
        return false;
    doc->layers = new_layers;
    doc->layers[doc->layer_count++] = *layer;
    return true;
}

static bool append_keyframe(scene_timeline_t *timeline, scene_keyframe_t *keyframe) {
    scene_keyframe_t *new_keyframes =
        realloc(timeline->keyframes, sizeof(scene_keyframe_t) * (timeline->keyframe_count + 1));
    if (!new_keyframes)
        return false;
    timeline->keyframes = new_keyframes;
    timeline->keyframes[timeline->keyframe_count++] = *keyframe;
    return true;
}

static bool append_group(scene_parse_ctx_t *ctx, const char *id) {
    if (!id || !*id)
        return true;
    for (size_t i = 0; i < ctx->group_count; i++) {
        if (strcmp(ctx->groups[i], id) == 0)
            return true;
    }
    char **new_groups = realloc(ctx->groups, sizeof(char *) * (ctx->group_count + 1));
    if (!new_groups)
        return false;
    ctx->groups = new_groups;
    ctx->groups[ctx->group_count] = strdup(id);
    if (!ctx->groups[ctx->group_count])
        return false;
    ctx->group_count++;
    return true;
}

static bool has_group(const scene_parse_ctx_t *ctx, const char *id) {
    for (size_t i = 0; i < ctx->group_count; i++) {
        if (strcmp(ctx->groups[i], id) == 0)
            return true;
    }
    return false;
}

static void free_groups(scene_parse_ctx_t *ctx) {
    for (size_t i = 0; i < ctx->group_count; i++)
        free(ctx->groups[i]);
    free(ctx->groups);
}

static bool starts_with_case(const char *value, const char *prefix) {
    return g_ascii_strncasecmp(value, prefix, strlen(prefix)) == 0;
}

static bool contains_case(const char *value, const char *needle) {
    char *lower_value = g_ascii_strdown(value, -1);
    char *lower_needle = g_ascii_strdown(needle, -1);
    bool found = strstr(lower_value, lower_needle) != NULL;
    g_free(lower_value);
    g_free(lower_needle);
    return found;
}

static char *trimmed_copy(const char *value) {
    while (*value && isspace((unsigned char)*value))
        value++;
    char *copy = strdup(value);
    if (!copy)
        return NULL;
    size_t len = strlen(copy);
    while (len > 0 && isspace((unsigned char)copy[len - 1]))
        copy[--len] = '\0';
    if ((copy[0] == '"' && len >= 2 && copy[len - 1] == '"') ||
        (copy[0] == '\'' && len >= 2 && copy[len - 1] == '\'')) {
        memmove(copy, copy + 1, len - 2);
        copy[len - 2] = '\0';
    }
    return copy;
}

static bool is_allowed_data_image_uri(const char *uri) {
    if (!starts_with_case(uri, "data:image/"))
        return false;
    const char *payload = strstr(uri, ";base64,");
    if (!payload)
        return false;
    payload += strlen(";base64,");
    if (*payload == '\0')
        return false;
    for (const char *p = payload; *p; p++) {
        if (isalnum((unsigned char)*p) || *p == '+' || *p == '/' || *p == '=' ||
            *p == '\r' || *p == '\n')
            continue;
        return false;
    }
    return true;
}

static bool validate_uri_value(const char *element,
                               const char *attribute,
                               const char *uri,
                               bool allow_data_image,
                               char error[SCENE_ERROR_LEN]) {
    char *trimmed = trimmed_copy(uri);
    if (!trimmed)
        return set_error(error, "out of memory");
    bool ok = true;
    if (trimmed[0] == '#') {
        ok = true;
    } else if (allow_data_image && is_allowed_data_image_uri(trimmed)) {
        ok = true;
    } else {
        ok = false;
    }
    if (!ok) {
        set_error(error, "external or unsafe URI on <%s> %s=\"%s\"",
                  element, attribute, trimmed);
    }
    free(trimmed);
    return ok;
}

static bool validate_css_urls(const char *text,
                              const char *where,
                              char error[SCENE_ERROR_LEN]) {
    if (contains_case(text, "@import"))
        return set_error(error, "CSS @import is not allowed in %s", where);

    const char *p = text;
    while ((p = strcasestr(p, "url(")) != NULL) {
        p += 4;
        const char *end = strchr(p, ')');
        if (!end)
            return set_error(error, "malformed CSS url(...) in %s", where);
        char *uri = strndup(p, (size_t)(end - p));
        if (!uri)
            return set_error(error, "out of memory");
        bool ok = validate_uri_value("style", "url", uri, false, error);
        free(uri);
        if (!ok)
            return false;
        p = end + 1;
    }
    return true;
}

static const char *xml_node_name(xmlNodePtr node) {
    return node && node->name ? (const char *)node->name : "unknown";
}

static bool validate_xml_resources(xmlNodePtr node, char error[SCENE_ERROR_LEN]) {
    for (xmlNodePtr cur = node; cur; cur = cur->next) {
        if (cur->type == XML_DTD_NODE)
            return set_error(error, "DOCTYPE declarations are not allowed");
        if (cur->type == XML_ENTITY_DECL)
            return set_error(error, "XML entity declarations are not allowed");
        if (cur->type == XML_PI_NODE) {
            const char *name = xml_node_name(cur);
            if (g_ascii_strcasecmp(name, "xml-stylesheet") == 0)
                return set_error(error, "XML stylesheet processing instructions are not allowed");
        }
        if (cur->type == XML_ELEMENT_NODE) {
            const char *name = xml_node_name(cur);
            if (g_ascii_strcasecmp(name, "include") == 0 &&
                cur->ns && cur->ns->href &&
                strcmp((const char *)cur->ns->href, "http://www.w3.org/2001/XInclude") == 0) {
                return set_error(error, "XML includes are not allowed");
            }

            for (xmlAttrPtr attr = cur->properties; attr; attr = attr->next) {
                xmlChar *value_xml = xmlNodeListGetString(cur->doc, attr->children, 1);
                if (!value_xml)
                    continue;
                const char *attr_name = (const char *)attr->name;
                const char *value = (const char *)value_xml;
                bool ok = true;
                bool is_href = g_ascii_strcasecmp(attr_name, "href") == 0 ||
                               g_ascii_strcasecmp(attr_name, "xlink:href") == 0;
                if (is_href) {
                    bool allow_data = g_ascii_strcasecmp(name, "image") == 0;
                    ok = validate_uri_value(name, attr_name, value, allow_data, error);
                } else if (g_ascii_strcasecmp(attr_name, "style") == 0) {
                    ok = validate_css_urls(value, "style attribute", error);
                } else if (strstr(value, "url(") || strstr(value, "URL(")) {
                    ok = validate_css_urls(value, attr_name, error);
                }
                xmlFree(value_xml);
                if (!ok)
                    return false;
            }
        }
        if ((cur->type == XML_TEXT_NODE || cur->type == XML_CDATA_SECTION_NODE) &&
            cur->parent && cur->parent->name &&
            g_ascii_strcasecmp((const char *)cur->parent->name, "style") == 0) {
            if (!validate_css_urls((const char *)cur->content, "style element", error))
                return false;
        }
        if (cur->children && !validate_xml_resources(cur->children, error))
            return false;
    }
    return true;
}

static xmlNodePtr find_group_by_id(xmlNodePtr node, const char *id) {
    for (xmlNodePtr cur = node; cur; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE && g_ascii_strcasecmp(xml_node_name(cur), "g") == 0) {
            xmlChar *value = xmlGetProp(cur, (const xmlChar *)"id");
            if (value) {
                bool match = strcmp((const char *)value, id) == 0;
                xmlFree(value);
                if (match)
                    return cur;
            }
        }
        xmlNodePtr child = find_group_by_id(cur->children, id);
        if (child)
            return child;
    }
    return NULL;
}

static void force_scene_container_visible(xmlNodePtr node) {
    xmlSetProp(node, (const xmlChar *)"display", (const xmlChar *)"inline");
    xmlSetProp(node, (const xmlChar *)"visibility", (const xmlChar *)"visible");

    xmlChar *style = xmlGetProp(node, (const xmlChar *)"style");
    const char *override = "display:inline !important; visibility:visible !important;";
    if (style && *style) {
        size_t len = strlen((const char *)style) + strlen(override) + 3;
        char *combined = malloc(len);
        if (combined) {
            snprintf(combined, len, "%s; %s", (const char *)style, override);
            xmlSetProp(node, (const xmlChar *)"style", (const xmlChar *)combined);
            free(combined);
        }
    } else {
        xmlSetProp(node, (const xmlChar *)"style", (const xmlChar *)override);
    }
    if (style)
        xmlFree(style);
}

static bool normalize_scene_svg(xmlDocPtr xml,
                                scene_document_t *doc,
                                char error[SCENE_ERROR_LEN]) {
    xmlNodePtr root = xmlDocGetRootElement(xml);
    if (!root)
        return set_error(error, "SVG document has no root element");

    for (size_t i = 0; i < doc->layer_count; i++) {
        xmlNodePtr layer = find_group_by_id(root, doc->layers[i].ref);
        if (!layer)
            return set_error(error, "layer '%s' references a missing SVG <g> element", doc->layers[i].ref);
        for (xmlNodePtr cur = layer; cur && cur->type == XML_ELEMENT_NODE; cur = cur->parent) {
            force_scene_container_visible(cur);
            if (cur == root)
                break;
        }
    }

    xmlChar *buffer = NULL;
    int len = 0;
    xmlDocDumpMemoryEnc(xml, &buffer, &len, "UTF-8");
    if (!buffer || len <= 0) {
        if (buffer)
            xmlFree(buffer);
        return set_error(error, "could not serialize normalized SVG");
    }
    doc->normalized_svg = malloc((size_t)len + 1);
    if (!doc->normalized_svg) {
        xmlFree(buffer);
        return set_error(error, "out of memory");
    }
    memcpy(doc->normalized_svg, buffer, (size_t)len);
    doc->normalized_svg[len] = '\0';
    doc->normalized_svg_len = (size_t)len;
    xmlFree(buffer);
    return true;
}

static bool reject_unsafe_xml_preamble(const char *contents,
                                       gsize len,
                                       char error[SCENE_ERROR_LEN]) {
    char *copy = g_strndup(contents, len);
    if (!copy)
        return set_error(error, "out of memory");
    bool bad_doctype = contains_case(copy, "<!doctype");
    bool bad_entity = contains_case(copy, "<!entity");
    bool bad_pi = contains_case(copy, "<?xml-stylesheet");
    g_free(copy);
    if (bad_doctype)
        return set_error(error, "DOCTYPE declarations are not allowed");
    if (bad_entity)
        return set_error(error, "XML entity declarations are not allowed");
    if (bad_pi)
        return set_error(error, "XML stylesheet processing instructions are not allowed");
    return true;
}

static bool parse_value_attrs(const char **names,
                              const char **values,
                              const char *prefix,
                              scene_value_set_t *out,
                              char error[SCENE_ERROR_LEN]) {
    memset(out, 0, sizeof(*out));
    const char *attrs[] = {
        "opacity", "translate-x", "translate-y", "scale", "scale-x", "scale-y", "rotation-deg", "reveal"
    };

    for (size_t i = 0; i < sizeof(attrs) / sizeof(attrs[0]); i++) {
        char key[64];
        if (prefix)
            snprintf(key, sizeof(key), "%s%s", prefix, attrs[i]);
        else
            snprintf(key, sizeof(key), "%s", attrs[i]);

        const char *value = attr_value(names, values, key);
        if (!value)
            continue;
        double parsed;
        if (!parse_double_full(value, &parsed))
            return set_error(error, "invalid numeric value for %s", key);

        if (strcmp(attrs[i], "opacity") == 0) {
            if (parsed < 0.0 || parsed > 1.0)
                return set_error(error, "opacity for %s is outside 0..1", key);
            out->has_opacity = true;
            out->opacity = parsed;
        } else if (strcmp(attrs[i], "translate-x") == 0) {
            out->has_translate_x = true;
            out->translate_x = parsed;
        } else if (strcmp(attrs[i], "translate-y") == 0) {
            out->has_translate_y = true;
            out->translate_y = parsed;
        } else if (strcmp(attrs[i], "scale") == 0) {
            if (parsed < 0.0)
                return set_error(error, "scale for %s must be non-negative", key);
            out->has_scale_x = true;
            out->has_scale_y = true;
            out->scale_x = parsed;
            out->scale_y = parsed;
        } else if (strcmp(attrs[i], "scale-x") == 0) {
            if (parsed < 0.0)
                return set_error(error, "scale-x for %s must be non-negative", key);
            out->has_scale_x = true;
            out->scale_x = parsed;
        } else if (strcmp(attrs[i], "scale-y") == 0) {
            if (parsed < 0.0)
                return set_error(error, "scale-y for %s must be non-negative", key);
            out->has_scale_y = true;
            out->scale_y = parsed;
        } else if (strcmp(attrs[i], "rotation-deg") == 0) {
            out->has_rotation_deg = true;
            out->rotation_deg = parsed;
        } else if (strcmp(attrs[i], "reveal") == 0) {
            if (parsed < 0.0 || parsed > 1.0)
                return set_error(error, "reveal for %s is outside 0..1", key);
            out->has_reveal = true;
            out->reveal = parsed;
        }
    }
    return true;
}

static bool parse_transition_attrs(const char **names,
                                   const char **values,
                                   const char *value_prefix,
                                   scene_transition_t *transition,
                                   char error[SCENE_ERROR_LEN]) {
    if (transition->present)
        return set_error(error, "duplicate transition declaration");
    memset(transition, 0, sizeof(*transition));
    transition->present = true;
    transition->easing = SCENE_EASE_LINEAR;

    const char *duration = attr_value(names, values, "duration-ms");
    if (!duration || !parse_int_positive(duration, &transition->duration_ms))
        return set_error(error, "transition duration-ms must be a positive integer");

    const char *easing = attr_value(names, values, "easing");
    if (!parse_easing(easing, &transition->easing))
        return set_error(error, "unknown transition easing '%s'", easing);

    return parse_value_attrs(names, values, value_prefix, &transition->values, error);
}

static bool parse_timeline_attrs(const char **names,
                                 const char **values,
                                 scene_timeline_t *timeline,
                                 char error[SCENE_ERROR_LEN]) {
    if (timeline->present)
        return set_error(error, "duplicate timeline declaration");
    memset(timeline, 0, sizeof(*timeline));
    timeline->present = true;
    timeline->easing = SCENE_EASE_LINEAR;

    const char *duration = attr_value(names, values, "duration-ms");
    if (!duration || !parse_int_positive(duration, &timeline->duration_ms))
        return set_error(error, "timeline duration-ms must be a positive integer");

    const char *loop = attr_value(names, values, "loop");
    if (!parse_bool_attr(loop, &timeline->loop))
        return set_error(error, "timeline loop must be true or false");

    const char *restart = attr_value(names, values, "restart");
    if (!parse_bool_attr(restart, &timeline->restart))
        return set_error(error, "timeline restart must be true or false");

    const char *easing = attr_value(names, values, "easing");
    if (!parse_easing(easing, &timeline->easing))
        return set_error(error, "unknown timeline easing '%s'", easing);

    const char *when = attr_value(names, values, "when");
    if (when) {
        if (strcmp(when, "visible") != 0)
            return set_error(error, "unsupported timeline when value '%s'", when);
        timeline->when_visible = true;
    }

    return true;
}

static bool parse_layer_attrs(scene_parse_ctx_t *ctx,
                              const char **names,
                              const char **values) {
    scene_layer_t layer;
    memset(&layer, 0, sizeof(layer));
    layer.blend = SCENE_BLEND_NORMAL;
    layer.svg_order = -1;

    const char *ref = attr_value(names, values, "ref");
    if (!ref || !*ref)
        return set_error(ctx->error, "layer is missing required ref");
    for (size_t i = 0; i < ctx->doc->layer_count; i++) {
        if (strcmp(ctx->doc->layers[i].ref, ref) == 0)
            return set_error(ctx->error, "duplicate layer declaration for '%s'", ref);
    }

    layer.ref = strdup(ref);
    if (!layer.ref)
        return set_error(ctx->error, "out of memory");

    const char *trigger = attr_value(names, values, "trigger");
    if (!parse_trigger(trigger, &layer.trigger)) {
        free(layer.ref);
        return set_error(ctx->error, "unknown trigger '%s'", trigger);
    }

    const char *states = attr_value(names, values, "states");
    if (states) {
        if (!parse_states(states, &layer.states)) {
            free(layer.ref);
            return set_error(ctx->error, "invalid states for layer '%s'", ref);
        }
    } else if (layer.trigger == SCENE_TRIGGER_NONE) {
        free(layer.ref);
        return set_error(ctx->error, "layer '%s' must declare states or trigger", ref);
    }

    const char *static_attr = attr_value(names, values, "static");
    if (!parse_bool_attr(static_attr, &layer.static_layer)) {
        free(layer.ref);
        return set_error(ctx->error, "static on layer '%s' must be true or false", ref);
    }

    const char *blend = attr_value(names, values, "blend");
    if (!parse_blend(blend, &layer.blend)) {
        free(layer.ref);
        return set_error(ctx->error, "unknown blend '%s' on layer '%s'", blend, ref);
    }

    const char *z = attr_value(names, values, "z");
    if (z) {
        if (!parse_double_full(z, &layer.z)) {
            free(layer.ref);
            return set_error(ctx->error, "invalid z on layer '%s'", ref);
        }
        layer.has_z = true;
    }

    const char *origin_x = attr_value(names, values, "origin-x");
    const char *origin_y = attr_value(names, values, "origin-y");
    if (origin_x || origin_y) {
        if (!origin_x || !origin_y ||
            !parse_double_full(origin_x, &layer.origin_x) ||
            !parse_double_full(origin_y, &layer.origin_y)) {
            free(layer.ref);
            return set_error(ctx->error, "layer '%s' origin-x and origin-y must both be numeric", ref);
        }
        layer.has_origin = true;
    }

    if (!append_layer(ctx->doc, &layer)) {
        free(layer.ref);
        return set_error(ctx->error, "out of memory");
    }
    ctx->current_layer = (ssize_t)ctx->doc->layer_count - 1;
    return true;
}

static int compare_layers(const void *a, const void *b) {
    const scene_layer_t *la = a;
    const scene_layer_t *lb = b;
    double za = la->has_z ? la->z : (double)la->svg_order;
    double zb = lb->has_z ? lb->z : (double)lb->svg_order;
    if (za < zb)
        return -1;
    if (za > zb)
        return 1;
    if (la->svg_order < lb->svg_order)
        return -1;
    if (la->svg_order > lb->svg_order)
        return 1;
    return 0;
}

static void start_element_cb(GMarkupParseContext *context,
                             const gchar *element_name,
                             const gchar **attribute_names,
                             const gchar **attribute_values,
                             gpointer user_data,
                             GError **error) {
    (void)context;
    (void)error;
    scene_parse_ctx_t *ctx = user_data;
    if (ctx->failed) {
        ctx->depth++;
        return;
    }

    const char *name = local_name(element_name);

    if (ctx->depth == 0) {
        if (strcmp(name, "svg") != 0) {
            parse_fail(ctx, "root element must be <svg>");
        } else {
            if (!parse_svg_dimension(attr_value(attribute_names, attribute_values, "width"), &ctx->doc->width) ||
                !parse_svg_dimension(attr_value(attribute_names, attribute_values, "height"), &ctx->doc->height)) {
                parse_fail(ctx, "svg width and height must be positive pixel dimensions");
            } else if (!parse_viewbox(attr_value(attribute_names, attribute_values, "viewBox"), ctx->doc)) {
                parse_fail(ctx, "invalid svg viewBox");
            }
        }
    }

    if (strcmp(name, "g") == 0) {
        const char *id = attr_value(attribute_names, attribute_values, "id");
        if (id) {
            if (!append_group(ctx, id))
                parse_fail(ctx, "out of memory");
            ctx->group_order++;
        }
    }

    if (strcmp(name, "image") == 0) {
        const char *href = attr_value(attribute_names, attribute_values, "href");
        if (!href)
            href = attr_value(attribute_names, attribute_values, "xlink:href");
        if (!href || strncmp(href, "data:", 5) != 0)
            parse_fail(ctx, "scene-v1 requires every <image> href to be an embedded data URI");
    }

    for (int i = 0; attribute_names && attribute_names[i]; i++) {
        if (strcmp(local_name(attribute_names[i]), "href") != 0)
            continue;
        const char *href = attribute_values[i];
        if (!href || href[0] == '#' || strncmp(href, "data:", 5) == 0)
            continue;
        parse_fail(ctx, "external href '%s' is not allowed in scene-v1", href);
    }

    if (strcmp(name, "metadata") == 0 && !ctx->in_metadata) {
        ctx->in_metadata = true;
        ctx->metadata_depth = ctx->depth;
    } else if (ctx->in_metadata && !ctx->in_scene && strcmp(name, "scene") == 0) {
        if (ctx->saw_scene) {
            parse_fail(ctx, "duplicate scene metadata");
        } else {
            ctx->saw_scene = true;
            ctx->in_scene = true;
            ctx->scene_depth = ctx->depth;
            const char *xmlns = attr_value(attribute_names, attribute_values, "xmlns");
            const char *version = attr_value(attribute_names, attribute_values, "version");
            if (xmlns && strcmp(xmlns, SCENE_NS) != 0) {
                parse_fail(ctx, "unsupported scene namespace '%s'", xmlns);
            } else if (!version || strcmp(version, "1") != 0) {
                parse_fail(ctx, "unsupported or missing scene version");
            } else if (!parse_fit(attr_value(attribute_names, attribute_values, "fit"), &ctx->doc->fit)) {
                parse_fail(ctx, "unsupported scene fit mode");
            } else if (!parse_hex_background(attr_value(attribute_names, attribute_values, "background"),
                                             &ctx->doc->background_r,
                                             &ctx->doc->background_g,
                                             &ctx->doc->background_b,
                                             &ctx->doc->background_a)) {
                parse_fail(ctx, "invalid scene background color");
            } else {
                ctx->doc->version = 1;
                ctx->doc->idle_fps = 30;
                const char *idle_fps = attr_value(attribute_names, attribute_values, "idle-fps");
                if (idle_fps && (!parse_int_positive(idle_fps, &ctx->doc->idle_fps) || ctx->doc->idle_fps > 60))
                    parse_fail(ctx, "scene idle-fps must be an integer from 1 to 60");
            }
        }
    } else if (ctx->in_scene) {
        if (strcmp(name, "layer") == 0 && ctx->depth == ctx->scene_depth + 1) {
            if (!parse_layer_attrs(ctx, attribute_names, attribute_values))
                ctx->failed = true;
        } else if (ctx->current_layer >= 0) {
            scene_layer_t *layer = &ctx->doc->layers[ctx->current_layer];
            if (strcmp(name, "enter") == 0) {
                if (!parse_transition_attrs(attribute_names, attribute_values, "from-", &layer->enter, ctx->error))
                    ctx->failed = true;
            } else if (strcmp(name, "exit") == 0) {
                if (!parse_transition_attrs(attribute_names, attribute_values, "to-", &layer->exit, ctx->error))
                    ctx->failed = true;
            } else if (strcmp(name, "timeline") == 0) {
                if (!parse_timeline_attrs(attribute_names, attribute_values, &layer->timeline, ctx->error))
                    ctx->failed = true;
                else
                    ctx->in_timeline = true;
            } else if (strcmp(name, "keyframe") == 0 && ctx->in_timeline) {
                scene_keyframe_t keyframe;
                memset(&keyframe, 0, sizeof(keyframe));
                const char *at = attr_value(attribute_names, attribute_values, "at");
                if (!at || !parse_double_full(at, &keyframe.at) ||
                    keyframe.at < 0.0 || keyframe.at > 1.0) {
                    parse_fail(ctx, "keyframe at must be a number in 0..1");
                } else if (!parse_value_attrs(attribute_names, attribute_values, NULL, &keyframe.values, ctx->error)) {
                    ctx->failed = true;
                } else if (!append_keyframe(&layer->timeline, &keyframe)) {
                    parse_fail(ctx, "out of memory");
                }
            }
        }
    }

    ctx->depth++;
}

static void end_element_cb(GMarkupParseContext *context,
                           const gchar *element_name,
                           gpointer user_data,
                           GError **error) {
    (void)context;
    (void)error;
    scene_parse_ctx_t *ctx = user_data;
    const char *name = local_name(element_name);
    int element_depth = ctx->depth - 1;

    if (ctx->in_timeline && strcmp(name, "timeline") == 0)
        ctx->in_timeline = false;
    if (ctx->current_layer >= 0 && strcmp(name, "layer") == 0)
        ctx->current_layer = -1;
    if (ctx->in_scene && strcmp(name, "scene") == 0 && element_depth == ctx->scene_depth)
        ctx->in_scene = false;
    if (ctx->in_metadata && strcmp(name, "metadata") == 0 && element_depth == ctx->metadata_depth)
        ctx->in_metadata = false;

    ctx->depth--;
}

static void passthrough_cb(GMarkupParseContext *context,
                           const gchar *passthrough_text,
                           gsize text_len,
                           gpointer user_data,
                           GError **error) {
    (void)context;
    (void)error;
    scene_parse_ctx_t *ctx = user_data;
    if (ctx->failed)
        return;
    if (text_len >= strlen("<?xml-stylesheet") &&
        g_strstr_len(passthrough_text, text_len, "<?xml-stylesheet")) {
        parse_fail(ctx, "external stylesheets are not allowed");
    }
}

static void text_cb(GMarkupParseContext *context,
                    const gchar *text,
                    gsize text_len,
                    gpointer user_data,
                    GError **error) {
    (void)context;
    (void)error;
    scene_parse_ctx_t *ctx = user_data;
    if (ctx->failed)
        return;
    if (g_strstr_len(text, text_len, "@import") ||
        g_strstr_len(text, text_len, "url(http:") ||
        g_strstr_len(text, text_len, "url(https:") ||
        g_strstr_len(text, text_len, "url(file:")) {
        parse_fail(ctx, "external stylesheet or resource reference is not allowed");
    }
}

static bool validate_document(scene_parse_ctx_t *ctx) {
    scene_document_t *doc = ctx->doc;
    if (!ctx->saw_scene)
        return set_error(ctx->error, "missing scene-v1 metadata");
    if (doc->layer_count == 0)
        return set_error(ctx->error, "scene must declare at least one layer");

    for (size_t i = 0; i < doc->layer_count; i++) {
        scene_layer_t *layer = &doc->layers[i];
        if (!has_group(ctx, layer->ref))
            return set_error(ctx->error, "layer '%s' references a missing <g id>", layer->ref);
        for (size_t g = 0; g < ctx->group_count; g++) {
            if (strcmp(ctx->groups[g], layer->ref) == 0) {
                layer->svg_order = (int)g;
                break;
            }
        }
        if (layer->static_layer) {
            if (layer->states != SCENE_STATE_MASK_ALL)
                return set_error(ctx->error, "static layer '%s' must use states=\"*\"", layer->ref);
            if (layer->trigger != SCENE_TRIGGER_NONE ||
                layer->enter.present ||
                layer->exit.present ||
                layer->timeline.present) {
                return set_error(ctx->error, "static layer '%s' cannot declare animations or triggers", layer->ref);
            }
        }
        if (layer->trigger != SCENE_TRIGGER_NONE && !layer->timeline.present)
            return set_error(ctx->error, "trigger layer '%s' must declare a timeline", layer->ref);
        if (layer->trigger != SCENE_TRIGGER_NONE && layer->timeline.when_visible)
            return set_error(ctx->error, "trigger layer '%s' cannot use when=\"visible\"", layer->ref);
        if (layer->trigger == SCENE_TRIGGER_NONE &&
            layer->timeline.present &&
            !layer->timeline.when_visible) {
            return set_error(ctx->error, "state layer '%s' timeline must use when=\"visible\"", layer->ref);
        }
        if (layer->timeline.present) {
            if (layer->timeline.keyframe_count == 0)
                return set_error(ctx->error, "timeline on layer '%s' must contain keyframes", layer->ref);
            double prev = -1.0;
            for (size_t k = 0; k < layer->timeline.keyframe_count; k++) {
                double at = layer->timeline.keyframes[k].at;
                if (at <= prev)
                    return set_error(ctx->error, "keyframes on layer '%s' must be strictly ordered by at", layer->ref);
                prev = at;
            }
        }
    }

    qsort(doc->layers, doc->layer_count, sizeof(scene_layer_t), compare_layers);
    return true;
}

bool scene_document_load_from_data(const char *display_path,
                                   const char *contents,
                                   size_t len,
                                   scene_document_t **out,
                                   char error[SCENE_ERROR_LEN]) {
    *out = NULL;
    if (error)
        error[0] = '\0';

    if (!reject_unsafe_xml_preamble(contents, len, error)) {
        return false;
    }
    if (len > INT_MAX) {
        return set_error(error, "scene SVG is too large to parse safely");
    }

    xmlDocPtr xml = xmlReadMemory(contents,
                                  (int)len,
                                  NULL,
                                  NULL,
                                  XML_PARSE_NONET);
    if (!xml) {
        return set_error(error, "invalid SVG XML");
    }
    if (xml->intSubset || xml->extSubset) {
        xmlFreeDoc(xml);
        return set_error(error, "DOCTYPE declarations are not allowed");
    }
    if (!validate_xml_resources(xml->children, error)) {
        xmlFreeDoc(xml);
        return false;
    }

    scene_document_t *doc = calloc(1, sizeof(*doc));
    if (!doc) {
        xmlFreeDoc(xml);
        return set_error(error, "out of memory");
    }
    doc->path = strdup(display_path ? display_path : "(memory)");
    doc->fit = SCENE_FIT_PIXEL;
    doc->background_a = 1.0;

    scene_parse_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.doc = doc;
    ctx.current_layer = -1;
    GError *gerr = NULL;

    GMarkupParser parser = {
        .start_element = start_element_cb,
        .end_element = end_element_cb,
        .text = text_cb,
        .passthrough = passthrough_cb,
        .error = NULL
    };

    GMarkupParseContext *markup =
        g_markup_parse_context_new(&parser, G_MARKUP_TREAT_CDATA_AS_TEXT, &ctx, NULL);
    gboolean ok = g_markup_parse_context_parse(markup, contents, len, &gerr);
    if (ok)
        ok = g_markup_parse_context_end_parse(markup, &gerr);
    g_markup_parse_context_free(markup);

    if (!ok) {
        set_error(error, "invalid SVG XML: %s", gerr ? gerr->message : "unknown parse error");
        if (gerr)
            g_error_free(gerr);
        free_groups(&ctx);
        xmlFreeDoc(xml);
        scene_document_free(doc);
        return false;
    }
    if (gerr)
        g_error_free(gerr);
    if (ctx.failed) {
        set_error(error, "%s", ctx.error);
        free_groups(&ctx);
        xmlFreeDoc(xml);
        scene_document_free(doc);
        return false;
    }
    if (!validate_document(&ctx)) {
        set_error(error, "%s", ctx.error);
        free_groups(&ctx);
        xmlFreeDoc(xml);
        scene_document_free(doc);
        return false;
    }

    if (!normalize_scene_svg(xml, doc, error)) {
        free_groups(&ctx);
        xmlFreeDoc(xml);
        scene_document_free(doc);
        return false;
    }
    doc->estimated_cache_bytes = (uint64_t)doc->layer_count *
                                 (uint64_t)doc->width *
                                 (uint64_t)doc->height *
                                 4u;

    free_groups(&ctx);
    xmlFreeDoc(xml);
    *out = doc;
    return true;
}

bool scene_document_load(const char *path, scene_document_t **out, char error[SCENE_ERROR_LEN]) {
    gchar *contents = NULL;
    gsize len = 0;
    GError *gerr = NULL;
    if (!g_file_get_contents(path, &contents, &len, &gerr)) {
        bool ret = set_error(error, "could not read scene '%s': %s", path, gerr ? gerr->message : "unknown error");
        if (gerr)
            g_error_free(gerr);
        return ret;
    }
    bool ok = scene_document_load_from_data(path, contents, len, out, error);
    g_free(contents);
    return ok;
}

void scene_document_free(scene_document_t *doc) {
    if (!doc)
        return;
    for (size_t i = 0; i < doc->layer_count; i++) {
        free(doc->layers[i].ref);
        free(doc->layers[i].timeline.keyframes);
    }
    free(doc->layers);
    free(doc->path);
    free(doc->normalized_svg);
    free(doc);
}

const char *scene_state_name(scene_state_t state) {
    switch (state) {
        case SCENE_STATE_LOCKED:
            return "locked";
        case SCENE_STATE_INPUT:
            return "input";
        case SCENE_STATE_VERIFY:
            return "verify";
        case SCENE_STATE_FAIL:
            return "fail";
        default:
            return "unknown";
    }
}

const char *scene_trigger_name(scene_trigger_t trigger) {
    switch (trigger) {
        case SCENE_TRIGGER_KEY:
            return "key";
        case SCENE_TRIGGER_BACKSPACE:
            return "backspace";
        case SCENE_TRIGGER_NONE:
        default:
            return "none";
    }
}

const char *scene_fit_name(scene_fit_t fit) {
    switch (fit) {
        case SCENE_FIT_PIXEL:
            return "pixel";
        case SCENE_FIT_CONTAIN:
            return "contain";
        case SCENE_FIT_COVER:
            return "cover";
        case SCENE_FIT_STRETCH:
            return "stretch";
        default:
            return "unknown";
    }
}

const char *scene_blend_name(scene_blend_t blend) {
    switch (blend) {
        case SCENE_BLEND_NORMAL:
            return "normal";
        case SCENE_BLEND_ADD:
            return "add";
        case SCENE_BLEND_MULTIPLY:
            return "multiply";
        case SCENE_BLEND_SCREEN:
            return "screen";
        case SCENE_BLEND_OVERLAY:
            return "overlay";
        case SCENE_BLEND_DARKEN:
            return "darken";
        case SCENE_BLEND_LIGHTEN:
            return "lighten";
        case SCENE_BLEND_DIFFERENCE:
            return "difference";
        case SCENE_BLEND_EXCLUSION:
            return "exclusion";
        default:
            return "unknown";
    }
}

void scene_document_print_summary(const scene_document_t *doc, const char *path) {
    size_t static_layers = 0;
    size_t triggers = 0;
    size_t timelines = 0;
    for (size_t i = 0; i < doc->layer_count; i++) {
        if (doc->layers[i].static_layer)
            static_layers++;
        if (doc->layers[i].trigger != SCENE_TRIGGER_NONE)
            triggers++;
        if (doc->layers[i].timeline.present)
            timelines++;
    }
    printf("Scene OK: %s\n", path);
    printf("version=%d fit=%s size=%.0fx%.0f layers=%zu static=%zu dynamic=%zu timelines=%zu triggers=%zu cache-estimate=%" PRIu64 " bytes\n",
           doc->version,
           scene_fit_name(doc->fit),
           doc->width,
           doc->height,
           doc->layer_count,
           static_layers,
           doc->layer_count - static_layers,
           timelines,
           triggers,
           doc->estimated_cache_bytes);
}

scene_fit_transform_t scene_fit_calculate(scene_fit_t fit,
                                          double scene_width,
                                          double scene_height,
                                          double dest_width,
                                          double dest_height) {
    scene_fit_transform_t tx = {1.0, 1.0, 0.0, 0.0};
    if (scene_width <= 0.0 || scene_height <= 0.0 || dest_width <= 0.0 || dest_height <= 0.0)
        return tx;

    switch (fit) {
        case SCENE_FIT_PIXEL:
            tx.scale_x = 1.0;
            tx.scale_y = 1.0;
            tx.translate_x = floor((dest_width - scene_width) / 2.0);
            tx.translate_y = floor((dest_height - scene_height) / 2.0);
            break;
        case SCENE_FIT_CONTAIN: {
            double scale = fmin(dest_width / scene_width, dest_height / scene_height);
            tx.scale_x = scale;
            tx.scale_y = scale;
            tx.translate_x = (dest_width - scene_width * scale) / 2.0;
            tx.translate_y = (dest_height - scene_height * scale) / 2.0;
            break;
        }
        case SCENE_FIT_COVER: {
            double scale = fmax(dest_width / scene_width, dest_height / scene_height);
            tx.scale_x = scale;
            tx.scale_y = scale;
            tx.translate_x = (dest_width - scene_width * scale) / 2.0;
            tx.translate_y = (dest_height - scene_height * scale) / 2.0;
            break;
        }
        case SCENE_FIT_STRETCH:
            tx.scale_x = dest_width / scene_width;
            tx.scale_y = dest_height / scene_height;
            tx.translate_x = 0.0;
            tx.translate_y = 0.0;
            break;
    }
    return tx;
}

double scene_ease_value(scene_easing_t easing, double t) {
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;
    switch (easing) {
        case SCENE_EASE_IN:
            return t * t;
        case SCENE_EASE_OUT:
            return 1.0 - (1.0 - t) * (1.0 - t);
        case SCENE_EASE_IN_OUT:
            if (t < 0.5)
                return 2.0 * t * t;
            return 1.0 - pow(-2.0 * t + 2.0, 2.0) / 2.0;
        case SCENE_EASE_LINEAR:
        default:
            return t;
    }
}

static double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

static bool value_has_property(const scene_value_set_t *set, int prop) {
    switch (prop) {
        case 0:
            return set->has_opacity;
        case 1:
            return set->has_translate_x;
        case 2:
            return set->has_translate_y;
        case 3:
            return set->has_scale_x;
        case 4:
            return set->has_scale_y;
        case 5:
            return set->has_rotation_deg;
        case 6:
            return set->has_reveal;
        default:
            return false;
    }
}

static double value_get_property(const scene_value_set_t *set, int prop) {
    switch (prop) {
        case 0:
            return set->opacity;
        case 1:
            return set->translate_x;
        case 2:
            return set->translate_y;
        case 3:
            return set->scale_x;
        case 4:
            return set->scale_y;
        case 5:
            return set->rotation_deg;
        case 6:
            return set->reveal;
        default:
            return 0.0;
    }
}

static void value_set_property(scene_value_set_t *set, int prop, double value) {
    switch (prop) {
        case 0:
            set->has_opacity = true;
            set->opacity = value;
            break;
        case 1:
            set->has_translate_x = true;
            set->translate_x = value;
            break;
        case 2:
            set->has_translate_y = true;
            set->translate_y = value;
            break;
        case 3:
            set->has_scale_x = true;
            set->scale_x = value;
            break;
        case 4:
            set->has_scale_y = true;
            set->scale_y = value;
            break;
        case 5:
            set->has_rotation_deg = true;
            set->rotation_deg = value;
            break;
        case 6:
            set->has_reveal = true;
            set->reveal = value;
            break;
    }
}

static bool keyframe_effective_property(const scene_timeline_t *timeline,
                                        size_t index,
                                        int prop,
                                        double *out) {
    for (size_t n = index + 1; n > 0; n--) {
        const scene_keyframe_t *keyframe = &timeline->keyframes[n - 1];
        if (value_has_property(&keyframe->values, prop)) {
            *out = value_get_property(&keyframe->values, prop);
            return true;
        }
    }
    return false;
}

bool scene_evaluate_keyframes(const scene_timeline_t *timeline,
                              int64_t elapsed_ms,
                              scene_value_set_t *out,
                              bool *finished) {
    memset(out, 0, sizeof(*out));
    if (finished)
        *finished = true;
    if (!timeline || !timeline->present || timeline->duration_ms <= 0 || timeline->keyframe_count == 0)
        return false;

    double progress;
    if (timeline->loop) {
        int64_t mod = elapsed_ms % timeline->duration_ms;
        if (mod < 0)
            mod += timeline->duration_ms;
        progress = (double)mod / (double)timeline->duration_ms;
        if (finished)
            *finished = false;
    } else {
        if (elapsed_ms >= timeline->duration_ms) {
            progress = 1.0;
            if (finished)
                *finished = true;
        } else {
            progress = elapsed_ms <= 0 ? 0.0 : (double)elapsed_ms / (double)timeline->duration_ms;
            if (finished)
                *finished = false;
        }
    }
    progress = scene_ease_value(timeline->easing, progress);

    const scene_keyframe_t *first = &timeline->keyframes[0];
    const scene_keyframe_t *last = &timeline->keyframes[timeline->keyframe_count - 1];
    const scene_keyframe_t *left = first;
    const scene_keyframe_t *right = last;

    if (progress <= first->at) {
        *out = first->values;
        return true;
    }
    if (progress >= last->at) {
        for (int prop = 0; prop < 7; prop++) {
            double value;
            if (keyframe_effective_property(timeline, timeline->keyframe_count - 1, prop, &value))
                value_set_property(out, prop, value);
        }
        return true;
    }

    size_t left_index = 0;
    for (size_t i = 1; i < timeline->keyframe_count; i++) {
        if (progress <= timeline->keyframes[i].at) {
            left = &timeline->keyframes[i - 1];
            right = &timeline->keyframes[i];
            left_index = i - 1;
            break;
        }
    }

    double span = right->at - left->at;
    double local_t = span <= 0.0 ? 1.0 : (progress - left->at) / span;
    for (int prop = 0; prop < 7; prop++) {
        double left_value = 0.0;
        bool has_left = keyframe_effective_property(timeline, left_index, prop, &left_value);
        bool has_right = value_has_property(&right->values, prop);
        if (has_left && has_right)
            value_set_property(out, prop, lerp(left_value,
                                               value_get_property(&right->values, prop),
                                               local_t));
        else if (has_left)
            value_set_property(out, prop, left_value);
        else if (has_right)
            value_set_property(out, prop, value_get_property(&right->values, prop));
    }
    return true;
}

static scene_layer_style_t base_style_for_layer(const scene_layer_t *layer) {
    scene_layer_style_t style;
    memset(&style, 0, sizeof(style));
    style.opacity = 1.0;
    style.scale_x = 1.0;
    style.scale_y = 1.0;
    style.reveal = 1.0;
    style.blend = layer->blend;
    style.visible = true;
    return style;
}

static void apply_values(scene_layer_style_t *style, const scene_value_set_t *values) {
    if (values->has_opacity)
        style->opacity = values->opacity;
    if (values->has_translate_x)
        style->translate_x = values->translate_x;
    if (values->has_translate_y)
        style->translate_y = values->translate_y;
    if (values->has_scale_x)
        style->scale_x = values->scale_x;
    if (values->has_scale_y)
        style->scale_y = values->scale_y;
    if (values->has_rotation_deg)
        style->rotation_deg = values->rotation_deg;
    if (values->has_reveal)
        style->reveal = values->reveal;
}

static scene_layer_style_t style_with_values(const scene_layer_t *layer, const scene_value_set_t *values) {
    scene_layer_style_t style = base_style_for_layer(layer);
    apply_values(&style, values);
    return style;
}

static scene_layer_style_t interpolate_style(scene_layer_style_t a,
                                             scene_layer_style_t b,
                                             scene_easing_t easing,
                                             double progress) {
    double t = scene_ease_value(easing, progress);
    scene_layer_style_t out = a;
    out.opacity = lerp(a.opacity, b.opacity, t);
    out.translate_x = lerp(a.translate_x, b.translate_x, t);
    out.translate_y = lerp(a.translate_y, b.translate_y, t);
    out.scale_x = lerp(a.scale_x, b.scale_x, t);
    out.scale_y = lerp(a.scale_y, b.scale_y, t);
    out.rotation_deg = lerp(a.rotation_deg, b.rotation_deg, t);
    out.reveal = lerp(a.reveal, b.reveal, t);
    out.origin_x = a.origin_x;
    out.origin_y = a.origin_y;
    out.blend = a.blend;
    out.visible = true;
    return out;
}

static bool layer_matches_state(const scene_layer_t *layer, scene_state_t state) {
    if (layer->trigger != SCENE_TRIGGER_NONE)
        return false;
    return (layer->states & (1u << state)) != 0;
}

scene_runtime_t *scene_runtime_create(const scene_document_t *doc) {
    scene_runtime_t *runtime = calloc(1, sizeof(*runtime));
    if (!runtime)
        return NULL;
    runtime->doc = doc;
    runtime->layers = calloc(doc->layer_count, sizeof(scene_layer_runtime_t));
    if (!runtime->layers) {
        free(runtime);
        return NULL;
    }
    scene_runtime_reset(runtime, SCENE_STATE_LOCKED, 0);
    return runtime;
}

void scene_runtime_free(scene_runtime_t *runtime) {
    if (!runtime)
        return;
    free(runtime->layers);
    free(runtime);
}

void scene_runtime_set_layer_origin(scene_runtime_t *runtime,
                                    size_t layer_index,
                                    double origin_x,
                                    double origin_y) {
    if (!runtime || layer_index >= runtime->doc->layer_count)
        return;
    scene_layer_runtime_t *lr = &runtime->layers[layer_index];
    lr->style.origin_x = origin_x;
    lr->style.origin_y = origin_y;
    lr->transition_from.origin_x = origin_x;
    lr->transition_from.origin_y = origin_y;
    lr->transition_to.origin_x = origin_x;
    lr->transition_to.origin_y = origin_y;
}

void scene_runtime_reset(scene_runtime_t *runtime, scene_state_t state, int64_t now_ms) {
    runtime->state = state;
    runtime->animating = false;
    for (size_t i = 0; i < runtime->doc->layer_count; i++) {
        const scene_layer_t *layer = &runtime->doc->layers[i];
        scene_layer_runtime_t *lr = &runtime->layers[i];
        memset(lr, 0, sizeof(*lr));
        lr->style = base_style_for_layer(layer);
        lr->actual_visible = layer_matches_state(layer, state);
        lr->desired_visible = lr->actual_visible;
        lr->style.visible = lr->actual_visible;
        if (!lr->actual_visible)
            lr->style.opacity = 0.0;
        lr->visible_since_ms = now_ms;
    }
}

static void start_transition(scene_layer_runtime_t *lr,
                             const scene_layer_t *layer,
                             scene_layer_style_t from,
                             scene_layer_style_t to,
                             bool end_visible,
                             int duration_ms,
                             int64_t now_ms) {
    if (duration_ms <= 0) {
        lr->transition_active = false;
        lr->style = to;
        lr->style.visible = end_visible;
        lr->actual_visible = end_visible;
        return;
    }
    lr->transition_active = true;
    lr->transition_start_ms = now_ms;
    lr->transition_duration_ms = duration_ms;
    lr->transition_from = from;
    lr->transition_to = to;
    lr->transition_from.blend = layer->blend;
    lr->transition_to.blend = layer->blend;
    lr->transition_end_visible = end_visible;
    lr->actual_visible = true;
}

void scene_runtime_set_state(scene_runtime_t *runtime, scene_state_t state, int64_t now_ms) {
    if (!runtime || state >= SCENE_STATE_COUNT)
        return;
    scene_runtime_evaluate(runtime, now_ms);
    runtime->state = state;

    for (size_t i = 0; i < runtime->doc->layer_count; i++) {
        const scene_layer_t *layer = &runtime->doc->layers[i];
        if (layer->trigger != SCENE_TRIGGER_NONE)
            continue;
        scene_layer_runtime_t *lr = &runtime->layers[i];
        bool target = layer_matches_state(layer, state);
        if (target == lr->desired_visible && !lr->transition_active)
            continue;
        lr->desired_visible = target;

        scene_layer_style_t current = lr->style;
        current.blend = layer->blend;
        current.visible = true;
        scene_layer_style_t base = base_style_for_layer(layer);
        base.origin_x = current.origin_x;
        base.origin_y = current.origin_y;
        scene_layer_style_t to = base;
        to.visible = true;

        if (target) {
            scene_layer_style_t from;
            if (lr->actual_visible || lr->transition_active) {
                from = current;
            } else if (layer->enter.present) {
                from = style_with_values(layer, &layer->enter.values);
                from.origin_x = current.origin_x;
                from.origin_y = current.origin_y;
            } else {
                from = base;
            }
            start_transition(lr,
                             layer,
                             from,
                             to,
                             true,
                             layer->enter.present ? layer->enter.duration_ms : 0,
                             now_ms);
            lr->visible_since_ms = now_ms;
        } else {
            scene_layer_style_t end = base;
            end.origin_x = current.origin_x;
            end.origin_y = current.origin_y;
            if (layer->exit.present)
                apply_values(&end, &layer->exit.values);
            else
                end.opacity = 0.0;
            start_transition(lr,
                             layer,
                             current,
                             end,
                             false,
                             layer->exit.present ? layer->exit.duration_ms : 0,
                             now_ms);
        }
        runtime->animating = runtime->animating || lr->transition_active;
    }
}

void scene_runtime_trigger(scene_runtime_t *runtime, scene_trigger_t trigger, int64_t now_ms) {
    if (!runtime || trigger == SCENE_TRIGGER_NONE)
        return;
    scene_runtime_evaluate(runtime, now_ms);
    for (size_t i = 0; i < runtime->doc->layer_count; i++) {
        const scene_layer_t *layer = &runtime->doc->layers[i];
        if (layer->trigger != trigger)
            continue;
        scene_layer_runtime_t *lr = &runtime->layers[i];
        if (lr->event_active && !layer->timeline.restart)
            continue;
        lr->event_active = true;
        lr->event_start_ms = now_ms;
        lr->actual_visible = true;
        lr->desired_visible = true;
        runtime->animating = true;
    }
}

bool scene_runtime_evaluate(scene_runtime_t *runtime, int64_t now_ms) {
    if (!runtime)
        return false;
    bool animating = false;

    for (size_t i = 0; i < runtime->doc->layer_count; i++) {
        const scene_layer_t *layer = &runtime->doc->layers[i];
        scene_layer_runtime_t *lr = &runtime->layers[i];
        scene_layer_style_t base = base_style_for_layer(layer);
        base.origin_x = lr->style.origin_x;
        base.origin_y = lr->style.origin_y;

        if (layer->trigger != SCENE_TRIGGER_NONE) {
            if (!lr->event_active) {
                lr->style = base;
                lr->style.visible = false;
                lr->style.opacity = 0.0;
                continue;
            }

            bool finished = false;
            scene_value_set_t values;
            scene_evaluate_keyframes(&layer->timeline, now_ms - lr->event_start_ms, &values, &finished);
            lr->style = base;
            apply_values(&lr->style, &values);
            lr->style.visible = true;
            if (finished) {
                lr->event_active = false;
                lr->actual_visible = false;
                lr->desired_visible = false;
                lr->style.visible = false;
            } else {
                animating = true;
            }
            continue;
        }

        if (lr->transition_active) {
            int64_t elapsed = now_ms - lr->transition_start_ms;
            double progress = elapsed <= 0 ? 0.0 : (double)elapsed / (double)lr->transition_duration_ms;
            scene_easing_t easing = lr->transition_end_visible ? layer->enter.easing : layer->exit.easing;
            if (progress >= 1.0) {
                lr->transition_active = false;
                lr->style = lr->transition_to;
                lr->style.visible = lr->transition_end_visible;
                lr->actual_visible = lr->transition_end_visible;
            } else {
                lr->style = interpolate_style(lr->transition_from, lr->transition_to, easing, progress);
                lr->style.visible = true;
                lr->actual_visible = true;
                animating = true;
            }
        } else {
            lr->actual_visible = lr->desired_visible;
            lr->style.visible = lr->actual_visible;
            if (!lr->actual_visible)
                lr->style.opacity = 0.0;
        }

        if (lr->style.visible && layer->timeline.present && layer->timeline.when_visible) {
            bool finished = false;
            scene_value_set_t values;
            scene_evaluate_keyframes(&layer->timeline, now_ms - lr->visible_since_ms, &values, &finished);
            apply_values(&lr->style, &values);
            if (layer->timeline.loop || !finished)
                animating = true;
        }
    }

    runtime->animating = animating;
    return animating;
}

bool scene_runtime_is_animating(const scene_runtime_t *runtime) {
    return runtime && runtime->animating;
}

scene_state_t scene_runtime_state(const scene_runtime_t *runtime) {
    return runtime ? runtime->state : SCENE_STATE_LOCKED;
}

const scene_layer_style_t *scene_runtime_layer_style(const scene_runtime_t *runtime,
                                                     size_t layer_index) {
    if (!runtime || layer_index >= runtime->doc->layer_count)
        return NULL;
    return &runtime->layers[layer_index].style;
}
