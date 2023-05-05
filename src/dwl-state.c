#include <getopt.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <poll.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-util.h>

#include "dwl-ipc-unstable-v1-protocol.h"
#include "xdg-output-unstable-v1-protocol.h"

#define VERSION 1.0
#define EQUAL 0
#define ERROR -1
#define POLLFDS 1
#define WL_ARRAY_LENGHT(array, type) ((array)->size/sizeof(type))
#define WL_ARRAY_AT(array, type, index) ((type)(array)->data+index)
#define CHECK_VERB_COUNT if (count > 1) { \
                            printf(" "); \
                            count--; \
                         }

/* Structures */
struct Tag {
    uint state; /* zdwl_ipc_output_v1_tag_state */
    uint client_amount;
    uint is_focused;
};

struct Monitor {
    char *xdg_name;
    int32_t registry_name;
    struct wl_list link;
    struct wl_output *wl_output;
    struct zdwl_ipc_output_v1 *dwl_output;
    struct Tag *tags;

    int framed;
    int active;
    int layout_index;
    char* title;
    char* appid;
};

enum Noun {
    Noun_None     = 0,

    Noun_All      = 1 << 1,
    Tags          = 1 << 2,
    Outputs       = 1 << 3,
    Active_Tag    = 1 << 4,
    Active_Output = 1 << 5,
};

enum Verb {
    Verb_None = 0,

    // Global
    Verb_All  = 1 << 1,
    State     = 1 << 2,
    No_Labels = 1 << 3,

    // Output specific
    Title     = 1 << 4,
    Appid     = 1 << 5,
    Layout    = 1 << 6,

    // Tag specific
    Focused   = 1 << 7,
    Clients   = 1 << 8,
};

/* Functions */
static void cleanup(void);
static void check_global(void* global, const char *msg);
static int  check_for_framed(char *name);
static int  check_for_multiple_verbs(int verbs);
static void die(const char* fmt, ...);
static void dwl_manager_tag(void *data, struct zdwl_ipc_manager_v1 *zdwl_ipc_manager_v1, const char *name);
static void dwl_manager_layout(void *data, struct zdwl_ipc_manager_v1 *zdwl_ipc_manager_v1, const char *name);
static void dwl_output_active(void *data, struct zdwl_ipc_output_v1 *zdwl_ipc_output_v1, uint32_t active);
static void dwl_output_frame(void *data, struct zdwl_ipc_output_v1 *zdwl_ipc_output_v1);
static void dwl_output_layout(void *data, struct zdwl_ipc_output_v1 *zdwl_ipc_output_v1, uint32_t layout);
static void dwl_output_tag(void *data, struct zdwl_ipc_output_v1 *zdwl_ipc_output_v1, uint32_t index, uint32_t state, uint32_t clients, uint32_t focused);
static void dwl_output_title(void *data, struct zdwl_ipc_output_v1 *zdwl_ipc_output_v1, const char *title);
static void dwl_output_appid(void *data, struct zdwl_ipc_output_v1 *zdwl_ipc_output_v1, const char *appid);
static void dwl_output_toggle_visibility(void *data, struct zdwl_ipc_output_v1 *zdwl_ipc_output_v1) {/* Do Nothing */}
static void *ecalloc(size_t amount, size_t size);
static struct Monitor *get_monitor_from_name(char *name);
static void global_add(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void global_remove(void *data, struct wl_registry *registry, uint32_t name);
static void monitor_cleanup(struct Monitor *monitor);
static void monitor_setup(uint32_t registry_name, struct wl_output* output);
static void monitor_output(struct Monitor *monitor, int tagmask);
static void setup(void);
static void print_wl_array(struct wl_array *array);
static void xdg_name(void* data, struct zxdg_output_v1* xdg_output, const char* name);

/* Variables */
static struct wl_display *display;
static int display_fd;
static struct zdwl_ipc_manager_v1* dwl_manager;
static struct wl_list monitors;
static struct pollfd *pollfds;
static struct zxdg_output_manager_v1* output_manager;
static struct wl_array tags,
                       layouts;
static int noun = 0,
           verb = 0;

/* Listeners */
static const struct zdwl_ipc_manager_v1_listener dwl_manager_listener = {
    .tag = dwl_manager_tag,
    .layout = dwl_manager_layout,
};

static const struct zdwl_ipc_output_v1_listener dwl_output_listener = {
    .tag = dwl_output_tag,
    .layout = dwl_output_layout,
    .title = dwl_output_title,
    .frame = dwl_output_frame,
    .active = dwl_output_active,
    .toggle_visibility = dwl_output_toggle_visibility,
    .appid = dwl_output_appid,
};

static const struct wl_registry_listener registry_listener = {
    .global = global_add,
    .global_remove = global_remove,
};

/* So that we can get the monitor names to match with dwl monitor names. */
static const struct zxdg_output_v1_listener xdg_output_listener = {
    .name = xdg_name,
};

void dwl_output_appid(void *data, struct zdwl_ipc_output_v1 *zdwl_ipc_output_v1, const char *appid) {
    struct Monitor *monitor = data;
    if (monitor->appid)
        free(monitor->appid);

    monitor->appid = strdup(appid);
}

int get_active_tags(struct Monitor *monitor) {
    int tagmask = 0;
    for (int i = 0; i < WL_ARRAY_LENGHT(&tags, char**); i++) {
        struct Tag *tag = &monitor->tags[i];
        if (tag->state & ZDWL_IPC_OUTPUT_V1_TAG_STATE_ACTIVE)
            tagmask |= 1 << i;
    }

    return tagmask;
}

struct Monitor *get_active_monitor(void) {
    struct Monitor *monitor;
    wl_list_for_each(monitor, &monitors, link) {
        if (monitor->active)
            return monitor;
    }

    return NULL;
}

void monitor_output(struct Monitor *monitor, int tagmask) {
    int i;
    int count = check_for_multiple_verbs(verb);

    if (!verb)
        return;

    if ((noun & Outputs || noun & Active_Output || noun & Noun_All) && (verb & State || verb & Appid || verb & Title || verb & Layout || verb & Verb_All)) {
        if (!(verb & No_Labels))
            printf("%s: ", monitor->xdg_name);

        if (verb & State || verb & Verb_All) {
            printf("%s", monitor->active ? "Active" : "InActive");
            CHECK_VERB_COUNT
        }

        if (verb & Title || verb & Verb_All) {
            printf("'%s'", monitor->title);
            CHECK_VERB_COUNT
        }

        if (verb & Appid || verb & Verb_All) {
            printf("'%s'", monitor->appid);
            CHECK_VERB_COUNT
        }

        if (verb & Layout || verb & Verb_All) {
            printf("%s", *WL_ARRAY_AT(&layouts, char**, monitor->layout_index));
            CHECK_VERB_COUNT
        }

        printf("\n");
    }

    if (!tagmask && !(noun & Active_Tag) && !(noun & Tags) && !(noun & Noun_All)) {
        return;
    }

    if (noun & Active_Tag)
        tagmask = get_active_tags(monitor);


    if (noun & Noun_All || noun & Tags) {
        tagmask = 0;
        for (i = 0; i < WL_ARRAY_LENGHT(&tags, char**); i++)
            tagmask |= 1 << i;
    }

    for (i = 0; i < WL_ARRAY_LENGHT(&tags, char**); i++) {
        if (!(tagmask & (1 << i)) || !(verb & Focused || verb & Clients || verb & Verb_All || verb & State))
            continue;

        if (!(verb & No_Labels))
            printf("%s: %d: ", monitor->xdg_name, i+1);

        struct Tag *tag = &monitor->tags[i];

        if (verb & State || verb & Verb_All) {
            printf("%s %s",
                    tag->state & ZDWL_IPC_OUTPUT_V1_TAG_STATE_ACTIVE ? "Active" : "InActive",
                    tag->state & ZDWL_IPC_OUTPUT_V1_TAG_STATE_URGENT ? "Urgent"  : "");
            CHECK_VERB_COUNT
        }

        if (verb & Focused || verb & Verb_All) {
            printf("%d ", tag->is_focused);
            CHECK_VERB_COUNT
        }

        if (verb & Clients || verb & Verb_All) {
            printf("%d", tag->client_amount);
            CHECK_VERB_COUNT
        }

        printf("\n");
    }
    fflush(stdout);
}

int check_for_framed(char *name) {
    int framed = 0;
    struct Monitor *monitor;

    wl_list_for_each(monitor, &monitors, link) {
        if (!name) { /* If no name check monitor */
            if (!monitor->framed)
                return 0;
            framed = 1;
            continue;
        }

        if (strcmp(monitor->xdg_name, name) == EQUAL && monitor->framed) {
            return 1;
        }
    }

    return framed;
}

int check_for_multiple_verbs(int verbs) {
    int count = 0;
    if (verbs & No_Labels)
        verbs ^= No_Labels;
    while (verbs) {
        if (verbs & 1)
            count++;
        verbs >>= 1;
    }
    return count;
}

void print_wl_array(struct wl_array *array) {
    char** ptr;
    wl_array_for_each(ptr, array) {
        printf("%s ", *ptr);
    }
    printf("\n");
    fflush(stdout);
}

struct Monitor *get_monitor_from_name(char *name) {
    struct Monitor *monitor;
    wl_list_for_each(monitor, &monitors, link) {
        if (strcmp(monitor->xdg_name, name) == EQUAL)
            return monitor;
    }

    return NULL;
}

void check_global(void *global, const char *msg) {
    if (!global)
        die(msg);
}

void xdg_name(void* data, struct zxdg_output_v1* xdg_output, const char* name) {
    struct Monitor *monitor = data;
    monitor->xdg_name = strdup(name);
    zxdg_output_v1_destroy(xdg_output);
}

void dwl_output_active(void *data, struct zdwl_ipc_output_v1 *zdwl_ipc_output_v1, uint32_t active) {
    struct Monitor* monitor = data;
    monitor->active = active;
}

void dwl_output_tag(void *data, struct zdwl_ipc_output_v1 *zdwl_ipc_output_v1, uint32_t index, uint32_t state, uint32_t clients, uint32_t focused) {
    struct Monitor *monitor = data;
    struct Tag *tag         = &monitor->tags[index];

    tag->state = state;
    tag->client_amount = clients;
    tag->is_focused = focused;
}

void dwl_output_layout(void *data, struct zdwl_ipc_output_v1 *zdwl_ipc_output_v1, uint32_t layout) {
    struct Monitor *monitor = data;
    monitor->layout_index=layout;
}

void dwl_output_title(void *data, struct zdwl_ipc_output_v1 *zdwl_ipc_output_v1, const char *title) {
    struct Monitor *monitor = data;
    if (monitor->title)
        free(monitor->title);

    monitor->title = strdup(title);
}

void dwl_output_frame(void *data, struct zdwl_ipc_output_v1 *zdwl_ipc_output_v1) {
    struct Monitor *monitor = data;
    monitor->framed = 1;
}

void dwl_manager_tag(void *data, struct zdwl_ipc_manager_v1 *zdwl_ipc_manager_v1, const char *name) {
    char** ptr = wl_array_add(&tags, sizeof(char**));
    if (!ptr)
        return;

    char* dup = strdup(name);

    memcpy(ptr, &dup, sizeof(char**));
}

void dwl_manager_layout(void *data, struct zdwl_ipc_manager_v1 *zdwl_ipc_manager_v1, const char *name) {
    char** ptr = wl_array_add(&layouts, sizeof(char**));
    if (!ptr)
        return;

    char* dup = strdup(name);

    memcpy(ptr, &dup, sizeof(char**));
}

void global_add(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    if (strcmp(interface, wl_output_interface.name) == EQUAL) {
        struct wl_output* output = wl_registry_bind(registry, name, &wl_output_interface, 1);
        monitor_setup(name, output);
        return;
    }
    if (strcmp(interface, zdwl_ipc_manager_v1_interface.name) == EQUAL) {
        dwl_manager = wl_registry_bind(registry, name, &zdwl_ipc_manager_v1_interface, 2);
        zdwl_ipc_manager_v1_add_listener(dwl_manager, &dwl_manager_listener, NULL);
        return;
    }
    if (strcmp(interface, zxdg_output_manager_v1_interface.name) == EQUAL) {
        output_manager = wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, 3);
        return;
    }
}

void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

void monitor_setup(uint32_t registry_name, struct wl_output* output) {
    struct Monitor* monitor = ecalloc(1, sizeof(*monitor));

    monitor->wl_output = output;
    monitor->registry_name = registry_name;
    monitor->active = 0;
    monitor->layout_index = 0;
    monitor->title = NULL;
    monitor->appid = NULL;

    zxdg_output_v1_add_listener(zxdg_output_manager_v1_get_xdg_output(output_manager, output),
                                &xdg_output_listener,
                                monitor);

    wl_list_insert(&monitors, &monitor->link);
}

void monitor_cleanup(struct Monitor *monitor) {
    free(monitor->tags);
    free(monitor->title);
    zdwl_ipc_output_v1_destroy(monitor->dwl_output);
}

void setup(void) {
    wl_array_init(&tags);
    wl_array_init(&layouts);
    wl_list_init(&monitors);

    display = wl_display_connect(NULL);
    if (!display)
        die("Could not connect to wayland display");

    display_fd = wl_display_get_fd(display);

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    check_global(output_manager, "output_manager");
    check_global(dwl_manager, "dwl_manager");

    wl_display_roundtrip(display);

    struct Monitor *monitor;
    wl_list_for_each(monitor, &monitors, link) {
        /*
         * We must initialize tags before we add dwl_output listener.
         */
        struct Tag *monitor_tags = ecalloc(WL_ARRAY_LENGHT(&tags, char**), sizeof(*monitor_tags));
        for (int i = 0; i < WL_ARRAY_LENGHT(&tags, char**); i++) {
            struct Tag *tag = &monitor_tags[i];
            tag->state = 0;
            tag->is_focused = 0;
            tag->client_amount = 0;
        }
        monitor->tags = monitor_tags;

        monitor->dwl_output = zdwl_ipc_manager_v1_get_output(dwl_manager, monitor->wl_output);
        zdwl_ipc_output_v1_add_listener(monitor->dwl_output, &dwl_output_listener, monitor);
    }

    wl_display_roundtrip(display);

    pollfds = ecalloc(POLLFDS, sizeof(*pollfds));

    pollfds[0] = (struct pollfd){display_fd, POLLIN};
}

int main(int argc, char *argv[]) {
    int i, opt = 0, tagmask = 0;
    char *wanted_monitor = NULL;
    struct Monitor *monitor;

    setup();

    while(opt != -1)  {
        opt = getopt(argc, argv, "vho:Ot:TeEaAsilLfcpn");
        switch (opt) {
            case 'v':
                printf("dwl-state %f\n", VERSION);
                goto done;
            case 'h':
                goto usage;
            case 'L':
                print_wl_array(&layouts);
                break;
            case 'e':
                verb |= Verb_All;
                break;
            case 'E':
                noun |= Noun_All;
                break;
            case 't':
                if (!(noun & Tags))
                    noun |= Tags;

                int tag = atoi(optarg);
                if (!tag || tag < 1 || tag > WL_ARRAY_LENGHT(&tags, char**))
                    die("%s is not a valid number or index", optarg);
                tagmask |= 1 << (tag-1);

                break;
            case 'T':
                noun |= Tags;
                break;
            case 'o':
                noun |= Outputs;

                if (!check_for_framed(optarg))
                    die("%s is not a valid monitor", optarg);
                wanted_monitor = optarg;

                break;
            case 'O':
                noun |= Outputs;
                break;
            case 'a':
                noun |= Active_Output;
                break;
            case 'A':
                noun |= Active_Tag;
                break;
            case 's':
                verb |= State;
                break;
            case 'i':
                verb |= Title;
                break;
            case 'l':
                verb |= Layout;
                break;
            case 'f':
                verb |= Focused;
                break;
            case 'c':
                verb |= Clients;
                break;
            case 'p':
                verb |= Appid;
                break;
            case 'n':
                verb |= No_Labels;
                break;
            case ':':
                goto usage;
            case '?':
                goto usage;
        }
    }

    if (!noun && !verb)
        goto done;

    if (noun & Outputs && !verb) {
        wl_list_for_each(monitor, &monitors, link) {
            printf("%s ", monitor->xdg_name);
        }
        printf("\n");
        goto done;
    }

    if (noun & Tags && !verb) {
        print_wl_array(&tags);
        goto done;
    }

    if ((noun & Tags || noun & Active_Tag) && !(verb & Focused || verb & Clients || verb & Verb_All))
        goto done;

    if ((noun & Outputs || noun & Active_Output) && !(verb & State || verb & Appid || verb & Title || verb & Layout || verb & Verb_All)) {
        goto done;
    }

    if ((!(noun & Tags) && !(noun & Active_Tag)) && (verb & Focused || verb & Clients)) {
        noun |= Active_Tag;
    }

    if ((!(noun & Outputs) && !wanted_monitor) || noun & Active_Output) {
        if (verb & State || verb & Appid || verb & Title || verb & Layout)
            noun |= Outputs;

        struct Monitor *active_monitor = get_active_monitor();
        if (!active_monitor)
            exit(EXIT_FAILURE);
        wanted_monitor = active_monitor->xdg_name;
    }

    if (noun & Noun_All || (noun & Outputs && !wanted_monitor)) {
        wl_list_for_each(monitor, &monitors, link) {
            monitor_output(monitor, tagmask);
        }
    } else {
        monitor = get_monitor_from_name(wanted_monitor);
        monitor_output(monitor, tagmask);
    }

    goto done;

usage:
    printf("Usage: %s [-option args]\n", argv[0]);
    printf("-h               -- Print this message.\n");
    printf("-v               -- Print the version and exit.\n");
    printf("--     Nouns     --\n");
    printf("-t [tag index]   -- Filter to a specific tag based on index.\n");
    printf("-T               -- Filter to all tags for an output.\n");
    printf("                    Use with no verbs to print tag names\n");
    printf("-o [output name] -- Filter to a specific output based on name.\n");
    printf("-O               -- Filter to all outputs, use along to print output names.\n");
    printf("                    Use with no verbs to print monitor names.\n");
    printf("-a               -- Filter to the active output.\n");
    printf("-A               -- Filter to the active tag.\n");
    printf("-E               -- Filter to all outputs and all tags same as -O and -T.\n");
    printf("--  Global Verbs --\n");
    printf("-L               -- Print available layout names.\n");
    printf("-s               -- Print the state of the object, tags if specified, output if no tags.\n");
    printf("-e               -- Print all information about a specified object, if no objects like tags or outputs specifed then print everything.\n");
    printf("-n               -- Print information with out labels.\n");
    printf("--  Output Verbs --\n");
    printf("-p               -- Get the appid of an output, if none specified get the active output.\n");
    printf("-i               -- Get the title of an output, if none specified get the active output.\n");
    printf("-l               -- Get the current layout of an output, if none specified get the active output.\n");
    printf("--   Tags Verbs  --\n");
    printf("-f               -- Get the focused state of a specifed tag, if none specified get the active tag.\n");
    printf("-c               -- Get the client amount of a specified tag, if none specified get the active tag.\n");

    fflush(stdout);

done:
    cleanup();
    exit(EXIT_SUCCESS);
}

void cleanup(void) {
    struct Monitor *monitor, *tmp;
    wl_list_for_each_safe(monitor, tmp, &monitors, link) {
        monitor_cleanup(monitor);
    }

    char** ptr;
    wl_array_for_each(ptr, &tags)
        free(*ptr);
    wl_array_for_each(ptr, &layouts)
        free(*ptr);
    wl_array_release(&tags);
    wl_array_release(&layouts);

    zdwl_ipc_manager_v1_destroy(dwl_manager);
    wl_display_disconnect(display);
}

void die(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);

    va_end(ap);

    if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
        fputc(' ', stderr);
        perror(NULL);
    } else {
        fputc('\n', stderr);
    }

    cleanup();
    exit(EXIT_FAILURE);
}

void *ecalloc(size_t amount, size_t size) {
    void *ptr = calloc(amount, size);

    if (!ptr)
        die("calloc did not allocate");

    return ptr;
}
