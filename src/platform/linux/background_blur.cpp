#include "background_blur.h"

#include <AUI/AppInfo.h>
#include <AUI/Logging/ALogger.h>

#if DIRTYRAMMARK_WAYLAND_BLUR

#include <dlfcn.h>
#include <wayland-client.h>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include "ext-background-effect-v1-client-protocol.h"
#include "kde-blur-client-protocol.h"

namespace {

constexpr auto LOG_TAG = "background_blur";

/// GTK and GDK are already loaded by AUI's GTK backend, so their symbols are looked up in the running process instead
/// of being linked to.
template <typename Fn>
Fn gtkSymbol(const char* name) {
    return reinterpret_cast<Fn>(dlsym(RTLD_DEFAULT, name));
}

struct Globals {
    wl_compositor* compositor = nullptr;
    ext_background_effect_manager_v1* ext = nullptr;
    uint32_t extCapabilities = 0;
    org_kde_kwin_blur_manager* kde = nullptr;
};

void extCapabilities(void* data, ext_background_effect_manager_v1*, uint32_t flags) {
    ALogger::info(LOG_TAG) << "ext_background_effect_manager_v1 capabilities: " << flags
                           << ((flags & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR) ? " (blur)" : " (no blur)");
    static_cast<Globals*>(data)->extCapabilities = flags;
}

void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto& g = *static_cast<Globals*>(data);
    ALogger::info(LOG_TAG) << "wl_registry global: " << interface << " v" << version;
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        g.compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 1));
    } else if (std::strcmp(interface, ext_background_effect_manager_v1_interface.name) == 0) {
        g.ext = static_cast<ext_background_effect_manager_v1*>(
            wl_registry_bind(registry, name, &ext_background_effect_manager_v1_interface, 1));
        // the capabilities event follows the bind right away, the listener must be there before it is dispatched
        static constexpr ext_background_effect_manager_v1_listener listener { extCapabilities };
        ext_background_effect_manager_v1_add_listener(g.ext, &listener, &g);
    } else if (std::strcmp(interface, org_kde_kwin_blur_manager_interface.name) == 0) {
        g.kde = static_cast<org_kde_kwin_blur_manager*>(
            wl_registry_bind(registry, name, &org_kde_kwin_blur_manager_interface, 1));
    }
}

void registryGlobalRemove(void*, wl_registry*, uint32_t) {}

/// Surfaces larger than this are blurred only partially; compositors clip the region to the surface.
constexpr int32_t REGION_SIZE = 1 << 14;

Globals gGlobals;   // objects live as long as the process, like the window
bool gBlurApplied = false;

bool requestBlur(wl_display* display, wl_surface* surface) {
    ALogger::info(LOG_TAG) << "looking for blur protocols in the Wayland registry";
    // GDK owns the default event queue of the display; use a private one to look at the registry.
    auto queue = wl_display_create_queue(display);
    auto wrapper = static_cast<wl_display*>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapper), queue);
    auto registry = wl_display_get_registry(wrapper);
    static constexpr wl_registry_listener registryListener { registryGlobal, registryGlobalRemove };
    wl_registry_add_listener(registry, &registryListener, &gGlobals);
    wl_display_roundtrip_queue(display, queue);   // globals
    wl_display_roundtrip_queue(display, queue);   // capabilities
    wl_registry_destroy(registry);
    wl_proxy_wrapper_destroy(wrapper);
    ALogger::info(LOG_TAG) << "wl_compositor=" << (gGlobals.compositor != nullptr)
                           << " ext_background_effect_manager_v1=" << (gGlobals.ext != nullptr)
                           << " org_kde_kwin_blur_manager=" << (gGlobals.kde != nullptr);
    if (gGlobals.ext && !(gGlobals.extCapabilities & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR)) {
        ALogger::info(LOG_TAG) << "ext-background-effect-v1 is advertised but blur is not among its capabilities";
    }
    if (gGlobals.ext && !gGlobals.compositor) {
        ALogger::warn(LOG_TAG) << "no wl_compositor, can't create a wl_region for ext-background-effect-v1";
    }

    if (gGlobals.ext && (gGlobals.extCapabilities & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR) &&
        gGlobals.compositor) {
        auto effect = ext_background_effect_manager_v1_get_background_effect(gGlobals.ext, surface);
        // null would remove the effect, so the whole surface is passed explicitly
        auto region = wl_compositor_create_region(gGlobals.compositor);
        wl_region_add(region, 0, 0, REGION_SIZE, REGION_SIZE);
        ext_background_effect_surface_v1_set_blur_region(effect, region);
        wl_region_destroy(region);
        ALogger::info(LOG_TAG) << "blur requested via ext-background-effect-v1";
        return true;
    }
    if (gGlobals.kde) {
        auto blur = org_kde_kwin_blur_manager_create(gGlobals.kde, surface);
        org_kde_kwin_blur_set_region(blur, nullptr);   // null is the whole surface
        org_kde_kwin_blur_commit(blur);
        ALogger::info(LOG_TAG) << "blur requested via org_kde_kwin_blur";
        return true;
    }
    return false;
}

/// GTK paints its own opaque background beneath AUI's texture.
void makeGtkWindowTransparent(void* display, void* gtkWindow) {
    auto providerNew = gtkSymbol<void* (*)()>("gtk_css_provider_new");
    auto loadFromData = gtkSymbol<void (*)(void*, const char*, intptr_t)>("gtk_css_provider_load_from_data");
    auto addProvider = gtkSymbol<void (*)(void*, void*, unsigned)>("gtk_style_context_add_provider_for_display");
    if (!providerNew || !loadFromData || !addProvider) {
        ALogger::warn(LOG_TAG) << "can't make the GTK window transparent: gtk_css_provider_* symbols not found";
        return;
    }
    constexpr unsigned PRIORITY_APPLICATION = 600;   // GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    auto provider = providerNew();
    loadFromData(
        provider,
        "window.background { background: transparent; }"
        // libadwaita (AdwToolbarView) paints the top bar on several nested nodes, so the translucent layer is drawn
        // once by the revealer that holds the bar; everything inside is transparent (otherwise layers would add up)
        "toolbarview > revealer { background-color: alpha(@headerbar_bg_color, 0.6); }"
        "toolbarview > revealer:backdrop { background-color: alpha(@headerbar_backdrop_color, 0.6); }"
        "toolbarview > revealer > *, toolbarview > revealer headerbar, toolbarview > revealer windowhandle {"
        "  background: transparent; box-shadow: none; }"
        // on a translucent bar the title looks too heavy
        "toolbarview > revealer headerbar .title { opacity: 0.75; }"
        // GTK headerbar (non-libadwaita window)
        "window > headerbar, window > .titlebar { background-color: alpha(@headerbar_bg_color, 0.6); }",
        -1);
    addProvider(display, provider, PRIORITY_APPLICATION);
    ALogger::info(LOG_TAG) << "GTK window background is transparent now";
}

/// What GNOME's Blur My Shell extension is going to do with this app.
enum class BlurMyShell {
    NOT_AVAILABLE,   ///< not GNOME or the extension is not enabled
    NOT_BLURRED,     ///< the extension is there, but doesn't blur this app (yet)
    BLURRED,         ///< the extension is configured to blur this app
    ASSUMED,         ///< the extension is there, but its settings can't be read
};

constexpr auto BLUR_MY_SHELL_UUID = "blur-my-shell@aunetx";
constexpr auto BLUR_MY_SHELL_SCHEMA = "org.gnome.shell.extensions.blur-my-shell.applications";

/// GNOME has no protocol for this; the only way to blur a window is the Blur My Shell extension, which blurs the
/// applications its user listed (Applications -> whitelist, or "blur all" with a blacklist). The extension is
/// considered present if it is enabled in the shell settings.
BlurMyShell blurMyShellState() {
    const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
    if (!desktop || !std::strstr(desktop, "GNOME")) {
        ALogger::info(LOG_TAG) << "XDG_CURRENT_DESKTOP=" << (desktop ? desktop : "(unset)") << ", not GNOME";
        return BlurMyShell::NOT_AVAILABLE;
    }
    using Source = void;
    auto sourceGetDefault = gtkSymbol<Source* (*)()>("g_settings_schema_source_get_default");
    auto sourceLookup = gtkSymbol<void* (*)(Source*, const char*, int)>("g_settings_schema_source_lookup");
    auto sourceFromDir =
        gtkSymbol<Source* (*)(const char*, Source*, int, void**)>("g_settings_schema_source_new_from_directory");
    auto sourceUnref = gtkSymbol<void (*)(Source*)>("g_settings_schema_source_unref");
    auto schemaUnref = gtkSymbol<void (*)(void*)>("g_settings_schema_unref");
    auto settingsNew = gtkSymbol<void* (*)(const char*)>("g_settings_new");
    auto settingsNewFull = gtkSymbol<void* (*)(void*, void*, const char*)>("g_settings_new_full");
    auto getStrv = gtkSymbol<char** (*)(void*, const char*)>("g_settings_get_strv");
    auto getBoolean = gtkSymbol<int (*)(void*, const char*)>("g_settings_get_boolean");
    auto strvContains = gtkSymbol<int (*)(const char* const*, const char*)>("g_strv_contains");
    auto strfreev = gtkSymbol<void (*)(char**)>("g_strfreev");
    auto objectUnref = gtkSymbol<void (*)(void*)>("g_object_unref");
    auto userDataDir = gtkSymbol<const char* (*)()>("g_get_user_data_dir");
    auto systemDataDirs = gtkSymbol<const char* const* (*)()>("g_get_system_data_dirs");
    auto errorFree = gtkSymbol<void (*)(void*)>("g_error_free");
    if (!sourceGetDefault || !sourceLookup || !sourceFromDir || !sourceUnref || !schemaUnref || !settingsNew ||
        !settingsNewFull || !getStrv || !getBoolean || !strvContains || !strfreev || !objectUnref || !userDataDir ||
        !systemDataDirs || !errorFree) {
        ALogger::warn(LOG_TAG) << "GNOME, but GIO symbols for GSettings not found, can't look for Blur My Shell";
        return BlurMyShell::NOT_AVAILABLE;
    }

    auto defaultSource = sourceGetDefault();
    auto shellSchema = defaultSource ? sourceLookup(defaultSource, "org.gnome.shell", 1) : nullptr;
    if (!shellSchema) {
        ALogger::warn(LOG_TAG) << "GNOME, but org.gnome.shell GSettings schema not found";
        return BlurMyShell::NOT_AVAILABLE;
    }
    schemaUnref(shellSchema);
    {
        auto shell = settingsNew("org.gnome.shell");
        auto enabled = getStrv(shell, "enabled-extensions");
        bool found = strvContains(enabled, BLUR_MY_SHELL_UUID);
        strfreev(enabled);
        objectUnref(shell);
        if (!found) {
            ALogger::info(LOG_TAG) << "GNOME, Blur My Shell is not enabled";
            return BlurMyShell::NOT_AVAILABLE;
        }
    }
    ALogger::info(LOG_TAG) << "GNOME, Blur My Shell is enabled";

    // 1. system-wide installed schema
    void* settings = nullptr;
    if (auto schema = sourceLookup(defaultSource, BLUR_MY_SHELL_SCHEMA, 1)) {
        ALogger::info(LOG_TAG) << "Blur My Shell schema: found among installed schemas";
        settings = settingsNewFull(schema, nullptr, nullptr);
        schemaUnref(schema);
    }
    // 2. schemas shipped inside the extension directory (an extension installed by the user doesn't register them)
    if (!settings) {
        std::vector<std::string> dataDirs;
        if (auto user = userDataDir()) {
            dataDirs.push_back(user);
        }
        for (auto dirs = systemDataDirs(); dirs && *dirs; ++dirs) {
            dataDirs.push_back(*dirs);
        }
        for (const auto& dir : dataDirs) {
            auto schemas = dir + "/gnome-shell/extensions/" + BLUR_MY_SHELL_UUID + "/schemas";
            void* error = nullptr;
            auto source = sourceFromDir(schemas.c_str(), defaultSource, 0, &error);
            if (!source) {
                if (error) {
                    errorFree(error);
                }
                ALogger::info(LOG_TAG) << "Blur My Shell schema: not in " << schemas;
                continue;
            }
            if (auto schema = sourceLookup(source, BLUR_MY_SHELL_SCHEMA, 0)) {
                ALogger::info(LOG_TAG) << "Blur My Shell schema: found in " << schemas;
                settings = settingsNewFull(schema, nullptr, nullptr);
                schemaUnref(schema);
            }
            sourceUnref(source);
            if (settings) {
                break;
            }
        }
    }
    if (!settings) {
        ALogger::warn(LOG_TAG) << "Blur My Shell schema " << BLUR_MY_SHELL_SCHEMA
                               << " not found, can't tell whether the app is blurred";
        return BlurMyShell::ASSUMED;
    }

    auto contains = [&](const char* key, const std::string& appId) {
        auto list = getStrv(settings, key);
        bool found = strvContains(list, appId.c_str());
        strfreev(list);
        return found;
    };
    const auto appId = aui::app_info::app_id.toStdString();
    const bool blur = getBoolean(settings, "blur");
    const bool enableAll = getBoolean(settings, "enable-all");
    const bool whitelisted = contains("whitelist", appId);
    const bool blacklisted = contains("blacklist", appId);
    objectUnref(settings);
    ALogger::info(LOG_TAG) << "Blur My Shell applications: blur=" << blur << " enable-all=" << enableAll << " "
                           << appId << " in whitelist=" << whitelisted << ", in blacklist=" << blacklisted;
    if (!blur) {
        ALogger::info(LOG_TAG) << "Blur My Shell: blur of applications is turned off in its settings";
        return BlurMyShell::NOT_BLURRED;
    }
    if (enableAll ? !blacklisted : whitelisted) {
        return BlurMyShell::BLURRED;
    }
    ALogger::info(LOG_TAG) << "Blur My Shell does not blur this app: add \"" << appId
                           << "\" to Blur My Shell -> Applications -> whitelist";
    return BlurMyShell::NOT_BLURRED;
}

}   // namespace

bool background_blur::enable(AWindow& window) {
    if (gBlurApplied) {
        ALogger::info(LOG_TAG) << "already applied";
        return true;   // one window is supported
    }
    auto getSurface = gtkSymbol<void* (*)(void*)>("gtk_native_get_surface");
    auto surfaceGetDisplay = gtkSymbol<void* (*)(void*)>("gdk_surface_get_display");
    auto waylandSurfaceType = gtkSymbol<size_t (*)()>("gdk_wayland_surface_get_type");
    auto isInstanceOf = gtkSymbol<int (*)(void*, size_t)>("g_type_check_instance_is_a");
    auto wlSurfaceOf = gtkSymbol<wl_surface* (*)(void*)>("gdk_wayland_surface_get_wl_surface");
    auto wlDisplayOf = gtkSymbol<wl_display* (*)(void*)>("gdk_wayland_display_get_wl_display");
    auto queueDraw = gtkSymbol<void (*)(void*)>("gtk_widget_queue_draw");
    if (!getSurface || !surfaceGetDisplay || !waylandSurfaceType || !isInstanceOf || !wlSurfaceOf || !wlDisplayOf ||
        !queueDraw) {
        ALogger::info(LOG_TAG) << "skipped: GTK/GDK Wayland symbols not found (not the GTK4 backend?)";
        return false;
    }

    // GtkWindow* on the GTK4 / libadwaita backend, see AWindow::nativeHandle
    auto gtkWindow = reinterpret_cast<void*>(window.nativeHandle());
    if (!gtkWindow) {
        ALogger::warn(LOG_TAG) << "skipped: window has no native handle (call after show()?)";
        return false;
    }
    auto gdkSurface = getSurface(gtkWindow);
    if (!gdkSurface) {
        ALogger::warn(LOG_TAG) << "skipped: GtkWindow has no GdkSurface (not realized yet? call after show())";
        return false;
    }
    if (!isInstanceOf(gdkSurface, waylandSurfaceType())) {
        ALogger::info(LOG_TAG) << "skipped: not a Wayland surface (X11 is not supported)";
        return false;
    }
    auto display = surfaceGetDisplay(gdkSurface);
    auto wlSurface = wlSurfaceOf(gdkSurface);
    auto wlDisplay = wlDisplayOf(display);
    if (!wlSurface || !wlDisplay) {
        ALogger::warn(LOG_TAG) << "skipped: no wl_surface (" << (wlSurface != nullptr) << ") or wl_display ("
                               << (wlDisplay != nullptr) << ")";
        return false;
    }

    if (!requestBlur(wlDisplay, wlSurface)) {
        ALogger::info(LOG_TAG) << "compositor supports neither ext-background-effect-v1 (blur) nor org_kde_kwin_blur";
        switch (blurMyShellState()) {
            case BlurMyShell::NOT_AVAILABLE:
            case BlurMyShell::NOT_BLURRED:
                ALogger::info(LOG_TAG) << "the window stays opaque";
                return false;
            case BlurMyShell::BLURRED:
                ALogger::info(LOG_TAG) << "Blur My Shell is going to blur the window";
                break;
            case BlurMyShell::ASSUMED:
                ALogger::info(LOG_TAG) << "Assuming user will add the app to Blur My Shell by themselves";
                break;
        }
    }
    makeGtkWindowTransparent(display, gtkWindow);
    queueDraw(gtkWindow);   // applies the state on the next wl_surface.commit
    wl_display_flush(wlDisplay);
    gBlurApplied = true;
    ALogger::info(LOG_TAG) << "done";
    return true;
}

#else

bool background_blur::enable(AWindow&) { return false; }

#endif
