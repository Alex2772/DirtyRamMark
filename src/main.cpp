#include <AUI/Platform/Entry.h>
#include <AUI/Platform/AWindow.h>
#include <AUI/Util/kAUI.h>
#include <AUI/ASS/AStylesheet.h>
#include <AUI/ASS/Property/BackgroundSolid.h>
#include <AUI/ASS/Property/BackgroundImage.h>
#include <AUI/ASS/Property/TextColor.h>
#include <AUI/View/ATitlebar.h>
#if AUI_PLATFORM_LINUX
#include <AUI/Platform/linux/APlatformAbstractionOptions.h>
#endif

#if __has_include(<AUI/Remote/AHotCodeReload.h>)
#include <AUI/Remote/AHotCodeReload.h>
#endif

#include "background_blur.h"
#include "cli.h"
#include "showcase.h"
#include "AUI/Util/Declarative/Containers.h"
#include "window_border.h"

using namespace ass;

AUI_ENTRY {
    if (auto exitCode = runCli(args)) {
        return *exitCode;
    }

#if AUI_PLATFORM_LINUX
    APlatformAbstractionOptions::set({
        .initializationOrder = {
            APlatformAbstractionOptions::Adwaita1{},
            APlatformAbstractionOptions::Gtk4{},
            APlatformAbstractionOptions::X11{},
        },
    });
#endif

    auto window = _new<AWindow>(AWindow::Init {
        .title = "DirtyRamMark {}"_format(AUI_PP_STRINGIZE(AUI_CMAKE_PROJECT_VERSION)),
        .width = 450_dp,
        .height = 430_dp,
#if AUI_PLATFORM_WIN
        // the contents (and the blur) extend under the titlebar, which is drawn by AUI
        .decoration = AWindow::Init::DecorationExtended {
            .buttons = {.minimize = true, .maximize = false, .close = true, },
        },
#else
        .decoration = AWindow::Init::DecorationDefault {
            .buttons = {.minimize = true, .maximize = false, .close = true, },
        },
#endif
        .resizable = false,
    });

    auto contents = declarative::Centered { ui::showcase(*window) } AUI_OVERRIDE_STYLE { Padding(0) };
#if AUI_PLATFORM_WIN
    window->setContents(declarative::Vertical {
        declarative::Titlebar { declarative::WindowTitle {} },
        contents,
    });
#else
    window->setContents(contents);
#endif

    window->show();

#if AUI_PLATFORM_WIN
    window_border::enable(*window);
#endif

#if AUI_PLATFORM_LINUX || AUI_PLATFORM_WIN
    // the window is translucent only if the compositor blurs it, otherwise the text would be hard to read
    if (background_blur::enable(*window)) {

#if AUI_PLATFORM_LINUX || AUI_PLATFORM_WIN
        window AUI_OVERRIDE_STYLE { BackgroundSolid { AColor(0xf0f0f0_rgb).transparentize(0.5f) } };
#endif
        // with the extended decoration the titlebar is drawn by AUI (the GTK one is hidden)
        AStylesheet::global().addRule({
            t<AView>(),
            FontRendering::ANTIALIASING,
        });
#if AUI_PLATFORM_WIN
        // AUI's titlebar is drawn on a solid OS-theme background with white text and icons; the window is translucent
        // and light here, so the titlebar is made see-through with dark contents.
        const AColor titlebarForeground = 0x202020_rgb;
        auto captionIcon = [&](const char* selector, const char* icon) {
            AStylesheet::global().addRules({
                {
                    t<ATitlebar>() >> class_of(selector),
                    BackgroundImage { icon, titlebarForeground, {}, Sizing::CENTER },
                },
            });
        };
        AStylesheet::global().addRules({
            {
                t<ATitlebar>(),
                BackgroundSolid { nullptr },
            },
            {
                t<ATitlebar>() >> t<AWindowTitle>(),
                TextColor { titlebarForeground },
            },
        });
        captionIcon(".minimize", ":uni/caption/minimize.svg");
        captionIcon(".close", ":uni/caption/close.svg");
        // the close button turns red on hover, the cross is white there
        AStylesheet::global().addRules({
            {
                t<ATitlebar>() >> class_of::hover(".close"),
                BackgroundImage { ":uni/caption/close.svg", AColor::WHITE, {}, Sizing::CENTER },
            },
            {
                t<ATitlebar>() >> class_of::active(".close"),
                BackgroundImage { ":uni/caption/close.svg", AColor::WHITE, {}, Sizing::CENTER },
            },
            {
                t<ATitlebar>() >> class_of(".nc-hover-close"),
                BackgroundImage { ":uni/caption/close.svg", AColor::WHITE, {}, Sizing::CENTER },
            },
        });
#endif
    }
#endif

    return 0;
};
