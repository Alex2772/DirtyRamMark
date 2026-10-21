#include "gauge.h"

#include <algorithm>
#include <array>
#include <random>
#include <vector>
#include <chrono>
#include <cmath>
#include <AUI/ASS/Property/Expanding.h>
#include <AUI/ASS/Property/MinSize.h>
#include <AUI/Font/AFontStyle.h>
#include <AUI/Image/IDrawable.h>
#include <AUI/Render/ABrush.h>
#include <AUI/Render/ARenderContext.h>
#include <AUI/Render/IRenderer.h>
#include <AUI/Render/RenderHints.h>
#include <AUI/Util/AAngleRadians.h>
#include <AUI/Util/UIBuildingHelpers.h>
#include <AUI/Util/kAUI.h>

using namespace ass;
using namespace std::chrono;

namespace {

// Angles are in degrees, clockwise from the top (12 o'clock), like AAngleRadians in IRenderer::squareSector.
constexpr float DIAL_BEGIN = 225.f;   // bottom left
constexpr float DIAL_SWEEP = 270.f;
constexpr float DIAL_SIZE_SCALE = 0.75f;   // the dial's size relative to the original; the text is not scaled
constexpr float SWEEP_SECONDS = 1.6f;   // duration of the needle test at startup

constexpr AColor COLOR_FACE = 0x1a1c21_rgb;
constexpr AColor COLOR_RIM = 0x3a3d45_rgb;
const AColor COLOR_TICK = AColor(0xe6e6e6_rgb).transparentize(0.4f);
constexpr AColor COLOR_GREEN = 0x2ecc71_rgb;
constexpr AColor COLOR_YELLOW = 0xf1c40f_rgb;
constexpr AColor COLOR_RED = 0xe74c3c_rgb;
constexpr AColor COLOR_MAROON = 0x8e0a2a_rgb;
constexpr AColor COLOR_NEEDLE = 0xff5a36_rgb;
constexpr AColor COLOR_MARKER = 0x404040_rgb;

glm::vec2 polar(glm::vec2 center, float radius, float clockDegrees) {
    const auto a = glm::radians(clockDegrees);
    return center + glm::vec2(std::sin(a), -std::cos(a)) * radius;
}

/// There is no IRenderer::drawArc: a ring segment is a ring (rounded rectangle border) masked by a sector.
void drawArc(ARenderContext& ctx, const AColor& color, glm::vec2 center, float radius, float thickness, float beginDeg, float endDeg) {
    if (endDeg <= beginDeg) {
        return;
    }
    const glm::vec2 size(radius * 2.f);
    const glm::vec2 position = center - size / 2.f;
    RenderHints::PushMask mask(ctx.render, [&] {
        ctx.render.squareSector(ASolidBrush { AColor::WHITE }, position, size, AAngleRadians(glm::radians(beginDeg)),
                                AAngleRadians(glm::radians(endDeg)));
    });
    ctx.render.roundedRectangleBorder(ASolidBrush { color }, position, size, radius, int(std::ceil(thickness)));
}

// assets/img/stresstest/bezel.png, see scripts/extract_stresstest_textures.py
constexpr float BEZEL_TEXTURE_SIZE = 316.f;     // px
constexpr float BEZEL_TEXTURE_OUTER_R = 156.f;  // px
constexpr float BEZEL_FACE_RATIO = 146.f / BEZEL_TEXTURE_OUTER_R;   // face goes slightly under the bezel's inner edge

/// Brushed-metal dial face filling the circle of the given radius.
void drawBackdrop(ARenderContext& ctx, glm::vec2 center, float radius) {
    static const _<IDrawable> backdrop = IDrawable::fromUrl(AUrl(":img/stresstest/backdrop.png"));
    if (!backdrop) {
        ctx.render.roundedRectangle(ASolidBrush { COLOR_FACE }, center - glm::vec2(radius), glm::vec2(radius * 2.f), radius);
        return;
    }
    IDrawable::Params p;
    p.offset = center - glm::vec2(radius);
    p.size = glm::vec2(radius * 2.f);
    backdrop->draw(ctx.render, p);
}

/// Draws the bezel ring so that its outer edge lies on the circle of the given radius.
void drawBezel(ARenderContext& ctx, glm::vec2 center, float radius) {
    static const _<IDrawable> bezel = IDrawable::fromUrl(AUrl(":img/stresstest/bezel.png"));
    if (!bezel) {
        // texture is missing: fall back to a plain rim
        ctx.render.roundedRectangleBorder(ASolidBrush { COLOR_RIM }, center - glm::vec2(radius), glm::vec2(radius * 2.f), radius, 3);
        return;
    }
    const float size = BEZEL_TEXTURE_SIZE * radius / BEZEL_TEXTURE_OUTER_R;
    IDrawable::Params p;
    p.offset = center - glm::vec2(size / 2.f);
    p.size = glm::vec2(size);
    bezel->draw(ctx.render, p);
}

// assets/img/stresstest/needle.png: points up, glow included; pivot and tip distance in px
constexpr float NEEDLE_TEXTURE_W = 100.f;
constexpr float NEEDLE_TEXTURE_H = 303.f;
constexpr float NEEDLE_PIVOT_X = 50.f;
constexpr float NEEDLE_PIVOT_Y = 249.f;
constexpr float NEEDLE_TIP_DISTANCE = 227.f;

/// Glowing needle with its hub, pointing at `clockDegrees`; its tip reaches `reach` from the center. False if the
/// texture is missing.
bool drawNeedle(ARenderContext& ctx, glm::vec2 center, float reach, float clockDegrees) {
    static const _<IDrawable> needle = IDrawable::fromUrl(AUrl(":img/stresstest/needle.png"));
    if (!needle) {
        return false;
    }
    const float k = reach / NEEDLE_TIP_DISTANCE;
    RenderHints::PushMatrix m(ctx.render);
    ctx.render.translate(center);
    ctx.render.rotate(AAngleRadians(glm::radians(clockDegrees)));
    IDrawable::Params p;
    p.offset = -glm::vec2(NEEDLE_PIVOT_X, NEEDLE_PIVOT_Y) * k;
    p.size = glm::vec2(NEEDLE_TEXTURE_W, NEEDLE_TEXTURE_H) * k;
    needle->draw(ctx.render, p);
    return true;
}

// assets/img/stresstest/fire/{flame,hot}.png, see scripts/generate_fire_sprites.py: flipbooks of a cluster of flame
// tongues; the hot (paler) one is for the ignition flash
constexpr int FIRE_FRAMES = 8;
constexpr float FIRE_FPS = 12.f;
constexpr float FIRE_ASPECT = 1.5f;   // height / width of a frame

/**
 * Fire made of particles. A particle is a big flame that sits on the rim of the dial, flickers (it is an animation:
 * the frames of the flipbook change) and burns out. The flames overlap, which makes a single body of fire. The strength
 * of the fire changes how many flames are alight, how big and how long-lived they are; a flame is always a complete
 * flame, not just its tip.
 * All coordinates are in dial radii relative to the center of the dial, so the fire does not care about the size.
 */
class Fire {
public:
    /// @param level strength of the fire in (0, 1]; 0 means that no new flames are born (the living ones die out)
    /// @param flare ignition flash in [0, 1]: more, bigger and hotter flames for a moment
    void update(float dt, float level, float flare) {
        for (auto& p : mParticles) {
            p.age += dt;
            p.pos.y -= p.rise * dt;
            p.pos.x += std::sin(p.age * 5.f + p.seed) * 0.12f * dt;   // flames waver
        }
        std::erase_if(mParticles, [](const Particle& p) { return p.age >= p.life; });
        if (level <= 0.f) {
            mSpawnDebt = 0.f;
            return;
        }
        mSpawnDebt += (7.f + 9.f * level) * (1.f + 2.f * flare) * dt;
        for (; mSpawnDebt >= 1.f; mSpawnDebt -= 1.f) {
            spawn(level, flare);
        }
    }

    void render(ARenderContext& ctx, glm::vec2 center, float radius) {
        static const _<IDrawable> flame = IDrawable::fromUrl(AUrl(":img/stresstest/fire/flame.png"));
        static const _<IDrawable> hot = IDrawable::fromUrl(AUrl(":img/stresstest/fire/hot.png"));

        const auto saved = ctx.render.getColor();
        for (const auto& p : mParticles) {
            const auto& drawable = p.hot ? hot : flame;
            if (!drawable) {
                continue;
            }
            const float u = p.age / p.life;   // 0 -> born, 1 -> burnt out
            const float grow = std::min(1.f, 0.5f + p.age * 4.f);   // flares up when born
            const float dying = std::max(0.f, (u - 0.55f) / 0.45f);   // the last part of the life: shrinks and fades
            const float scale = grow * (1.f - 0.45f * dying * dying);
            const float alpha = std::min(1.f, p.age * 10.f) * (1.f - dying * dying);
            const float width = p.size * scale * radius;
            const float height = width * FIRE_ASPECT;
            const int frame = int(p.age * FIRE_FPS + p.phase) % FIRE_FRAMES;
            ctx.render.setColorForced(saved * AColor(1.f, 1.f, 1.f, alpha));
            IDrawable::Params params;
            params.offset = center + p.pos * radius - glm::vec2(width / 2.f, height);   // pos is the base of the flame
            params.size = { width, height };
            params.cropUvTopLeft = glm::vec2(float(frame) / FIRE_FRAMES, 0.f);
            params.cropUvBottomRight = glm::vec2(float(frame + 1) / FIRE_FRAMES, 1.f);
            drawable->draw(ctx.render, params);
        }
        ctx.render.setColorForced(saved);
    }

    bool active() const { return !mParticles.empty(); }
    size_t count() const { return mParticles.size(); }

private:
    struct Particle {
        glm::vec2 pos;
        float rise = 0.f;
        float age = 0.f;
        float life = 1.f;
        float size = 0.4f;
        float seed = 0.f;
        float phase = 0.f;   // which frame it starts with, so the flames don't flicker in sync
        bool hot = false;
    };
    std::vector<Particle> mParticles;
    float mSpawnDebt = 0.f;
    std::mt19937 mRng { std::random_device {}() };

    float random(float from, float to) { return std::uniform_real_distribution<float>(from, to)(mRng); }

    void spawn(float level, float flare) {
        Particle p;
        // the flames sit on the rim, everywhere except the bottom (it is hidden behind the dial anyway)
        const float angle = glm::radians(random(-115.f, 115.f));
        p.pos = glm::vec2(std::sin(angle), -std::cos(angle)) * 0.85f;
        p.rise = (0.10f + 0.10f * level + 0.25f * flare) * random(0.7f, 1.3f);
        p.life = (0.8f + 0.3f * level) * (1.f + 0.25f * flare) * random(0.8f, 1.25f);
        p.size = (0.40f + 0.12f * level + 0.14f * flare) * random(0.85f, 1.15f);
        p.seed = random(0.f, 6.28f);
        p.phase = random(0.f, float(FIRE_FRAMES));
        p.hot = random(0.f, 1.f) < flare * 0.8f;
        mParticles.push_back(p);
    }
};

class GaugeView : public AView {
public:
    AProperty<float> value = 0.f;
    AProperty<float> min = 0.f;
    AProperty<float> max = 1.f;
    AProperty<float> yellowFrom = 1.f;
    AProperty<float> redFrom = 1.f;
    AProperty<float> maroonFrom = 1.f;
    float fireFrom = 0.f;
    float fireFull = 0.f;
    AString title;
    AString unit;
    int precision = 0;
    int scalePrecision = 0;
    int majorTicks = 10;
    bool jitter = true;
    AVector<declarative::Gauge::Marker> markers;

    GaugeView() {
        const auto redrawOnChange = [this](float) { redraw(); };
        connect(value.changed, this, redrawOnChange);
        connect(min.changed, this, redrawOnChange);
        connect(max.changed, this, redrawOnChange);
        connect(yellowFrom.changed, this, redrawOnChange);
        connect(redFrom.changed, this, redrawOnChange);
        connect(maroonFrom.changed, this, redrawOnChange);
    }

    void render(ARenderContext ctx) override {
        AView::render(ctx);

        const auto now = steady_clock::now();
        const float dt = mLastFrame ? duration<float>(now - *mLastFrame).count() : 0.f;
        mLastFrame = now;

        const float lo = *min;
        const float hi = std::max(*max, lo + 1e-6f);
        // the needle and the readout are not limited by the scale: it may go past the end of the dial
        const auto unclampedFraction = [&](float v) { return (v - lo) / (hi - lo); };
        // the static parts of the dial (zones, markers) stay on the scale
        const auto fraction = [&](float v) { return std::clamp(unclampedFraction(v), 0.f, 1.f); };

        // ignition test: right after the window shows up the needle sweeps to the end of the scale and back
        if (!mStart) {
            mStart = now;
        }
        const float elapsed = duration<float>(now - *mStart).count();
        const bool sweeping = elapsed < SWEEP_SECONDS;
        const float target = unclampedFraction(*value);
        if (sweeping) {
            const float p = elapsed / SWEEP_SECONDS;
            const float triangle = p < 0.5f ? p * 2.f : 2.f - p * 2.f;
            mShown = triangle * triangle * (3.f - 2.f * triangle);   // smoothstep
        } else {
            // damped needle: it revs up, it doesn't teleport
            mShown += (target - mShown) * (1.f - std::exp(-dt * 7.f));
        }
        const bool settled = !sweeping && std::abs(target - mShown) < 0.0005f;
        if (settled) {
            mShown = target;
        }
        // engine vibration: the harder the load, the more the needle trembles
        const float shake = jitter && !sweeping && mShown > 0.05f
            ? std::sin(duration<float>(now.time_since_epoch()).count() * 70.f) * 0.004f * mShown
            : 0.f;

        const bool fireEnabled = fireFrom < fireFull;
        const float size = float(std::min(getWidth(), getHeight()));
        // markers are drawn outside of the dial: leave some room for them
        const float radius = markers.empty() ? size / 2.f - 2.f : size * 0.5f * 0.64f;
        const glm::vec2 center = glm::vec2(getSize()) / 2.f;
        // the dial is smaller than it used to be, but the text keeps its size
        const float textRadius = radius / DIAL_SIZE_SCALE;
        const float ring = radius * 0.09f;
        const float scaleRadius = radius * 0.90f;   // arcs are drawn from the outer border, so see the outer radius

        // the fire: under the dial, so only the flames above and beside it are visible
        const float needleValue = lo + mShown * (hi - lo);   // follows the needle, not the raw value
        const bool burning = fireEnabled && !sweeping && needleValue >= fireFrom;
        const float fireLevel =
            burning ? 0.12f + 0.88f * std::clamp((needleValue - fireFrom) / (fireFull - fireFrom), 0.f, 1.f) : 0.f;
        if (burning && !mWasBurning && mFire.count() < 8) {
            mIgnitedAt = now;   // catches fire (not just flickers around the threshold)
        }
        mWasBurning = burning;
        const float sinceIgnition = duration<float>(now - mIgnitedAt).count();
        // flash: a quick rise, then a slower decay to the steady fire
        const float flare = sinceIgnition < FLARE_RISE ? sinceIgnition / FLARE_RISE : std::exp(-(sinceIgnition - FLARE_RISE) * FLARE_DECAY);
        mFire.update(std::min(dt, 0.05f), fireLevel, flare);
        mFire.render(ctx, center, radius);

        // face: tucked under the metal bezel
        const float inner = radius * BEZEL_FACE_RATIO;
        drawBackdrop(ctx, center, inner);

        // colored zones
        struct Zone {
            float from;
            AColor color;
        };
        const Zone zones[] = {
            { lo, COLOR_GREEN },
            { *yellowFrom, COLOR_YELLOW },
            { *redFrom, COLOR_RED },
            { *maroonFrom, COLOR_MAROON },
        };
        for (size_t i = 0; i < std::size(zones); ++i) {
            const float from = std::max(zones[i].from, lo);
            const float to = i + 1 < std::size(zones) ? std::min(zones[i + 1].from, hi) : hi;
            if (from >= hi || to <= from) {
                continue;
            }
            drawArc(ctx, zones[i].color, center, scaleRadius, ring, DIAL_BEGIN + fraction(from) * DIAL_SWEEP,
                    DIAL_BEGIN + fraction(to) * DIAL_SWEEP);
        }

        // scale
        AFontStyle numbers;
        numbers.size = unsigned(std::max(8.f, textRadius * 0.11f));
        const int MAJOR = std::max(1, majorTicks);
        constexpr int MINOR_PER_MAJOR = 5;
        for (int i = 0; i <= MAJOR * MINOR_PER_MAJOR; ++i) {
            const bool major = i % MINOR_PER_MAJOR == 0;
            const float angle = DIAL_BEGIN + float(i) / float(MAJOR * MINOR_PER_MAJOR) * DIAL_SWEEP;
            const float outerR = scaleRadius - ring - 2.f;
            const float tick = radius * (major ? 0.10f : 0.05f);
            ctx.render.line(ASolidBrush { COLOR_TICK }, polar(center, outerR, angle), polar(center, outerR - tick, angle),
                            ABorderStyle::Solid {}, AMetric(major ? 1.5f : 1.f, AMetric::T_PX));
            if (major) {
                const auto text = "{:.{}f}"_format(lo + float(i / MINOR_PER_MAJOR) / float(MAJOR) * (hi - lo), scalePrecision);
                const float width = float(numbers.getWidth(text));
                const auto p = polar(center, outerR - tick - radius * 0.11f, angle);
                drawText(ctx, text, p - glm::vec2(width / 2.f, -float(numbers.size) * 0.35f), numbers, COLOR_TICK);
            }
        }

        drawMarkers(ctx, center, radius, lo + mShown * (hi - lo), fraction);

        // caption and digital readout
        AFontStyle caption;
        caption.size = unsigned(std::max(8.f, textRadius * 0.13f));
        caption.bold = true;
        centeredText(ctx, title, center + glm::vec2(0.f, -radius * 0.27f), caption, COLOR_TICK.transparentize(0.25f));

        AFontStyle readout;
        readout.size = unsigned(std::max(10.f, textRadius * 0.22f));
        readout.bold = true;
        const float shownValue = lo + mShown * (hi - lo);
        centeredText(ctx, "{:.{}f}"_format(shownValue, precision), center + glm::vec2(0.f, radius * 0.45f), readout, COLOR_TICK);

        AFontStyle unitStyle;
        unitStyle.size = unsigned(std::max(8.f, textRadius * 0.11f));
        centeredText(ctx, unit, center + glm::vec2(0.f, radius * 0.70f), unitStyle, COLOR_TICK.transparentize(0.4f));

        // needle
        const float angle = DIAL_BEGIN + (mShown + shake) * DIAL_SWEEP;
        if (!drawNeedle(ctx, center, radius * 0.80f, angle)) {
            ctx.render.line(ASolidBrush { COLOR_NEEDLE }, polar(center, -radius * 0.14f, angle), polar(center, radius * 0.80f, angle),
                            ABorderStyle::Solid {}, AMetric(std::max(2.f, radius * 0.035f), AMetric::T_PX));
            const float hub = radius * 0.08f;
            ctx.render.roundedRectangle(ASolidBrush { COLOR_RIM }, center - glm::vec2(hub), glm::vec2(hub * 2.f), hub);
            const float hubInner = hub * 0.55f;
            ctx.render.roundedRectangle(ASolidBrush { COLOR_NEEDLE }, center - glm::vec2(hubInner), glm::vec2(hubInner * 2.f), hubInner);
        }
        drawBezel(ctx, center, radius);
        if (!settled || shake != 0.f || mFire.active()) {
            redraw();
        } else {
            mLastFrame.reset();
        }
    }

private:
    AOptional<steady_clock::time_point> mLastFrame;
    AOptional<steady_clock::time_point> mStart;
    float mShown = 0.f;
    Fire mFire;
    bool mWasBurning = false;
    steady_clock::time_point mIgnitedAt;
    static constexpr float FLARE_RISE = 0.12f;   // seconds
    static constexpr float FLARE_DECAY = 3.2f;   // 1/seconds: the flash is gone in about a second

    /// Reference points around the dial: a tick on the rim and a label, bright once the needle has passed it.
    void drawMarkers(ARenderContext& ctx, glm::vec2 center, float radius, float shownValue, const auto& fraction) {
        AFontStyle style;
        style.size = unsigned(std::max(9.f, radius / DIAL_SIZE_SCALE * 0.095f));
        for (const auto& marker : markers) {
            const float angle = DIAL_BEGIN + fraction(marker.value) * DIAL_SWEEP;
            const bool reached = shownValue >= marker.value;
            const AColor color = reached ? COLOR_MARKER : COLOR_MARKER.transparentize(0.55f);
            ctx.render.line(ASolidBrush { color }, polar(center, radius + 1.f, angle), polar(center, radius + radius * 0.07f, angle),
                            ABorderStyle::Solid {}, AMetric(reached ? 3.f : 2.f, AMetric::T_PX));
            const float dot = reached ? 3.5f : 2.5f;
            const auto tip = polar(center, radius + radius * 0.07f + dot, angle);
            ctx.render.roundedRectangle(ASolidBrush { color }, tip - glm::vec2(dot), glm::vec2(dot * 2.f), dot);

            const auto at = polar(center, radius + radius * 0.07f + dot * 2.f + 3.f, angle);
            const float side = std::sin(glm::radians(angle));
            const float width = float(style.getWidth(marker.label));
            float x = at.x;
            if (side > 0.35f) {
                // right half: the text grows away from the dial
            } else if (side < -0.35f) {
                x -= width;
            } else {
                x -= width / 2.f;
            }
            const float up = -std::cos(glm::radians(angle));   // -1 at the top, +1 at the bottom
            drawText(ctx, marker.label, { x, at.y + float(style.size) * (0.35f + 0.4f * up) }, style, color);
        }
    }

    static void drawText(ARenderContext& ctx, const AString& text, glm::vec2 baseline, const AFontStyle& style, const AColor& color) {
        const auto saved = ctx.render.getColor();
        ctx.render.setColor(color);
        ctx.render.string(baseline, text, style);
        ctx.render.setColorForced(saved);
    }

    static void centeredText(ARenderContext& ctx, const AString& text, glm::vec2 center, const AFontStyle& style, const AColor& color) {
        const float width = float(style.getWidth(text));
        drawText(ctx, text, center - glm::vec2(width / 2.f, -float(style.size) * 0.35f), style, color);
    }
};

}   // namespace

_<AView> declarative::Gauge::operator()() {
    auto view = _new<GaugeView>();
    view->title = std::move(title);
    view->unit = std::move(unit);
    view->precision = precision;
    view->scalePrecision = scalePrecision;
    view->majorTicks = majorTicks;
    view->jitter = jitter;
    view->markers = std::move(markers);
    view->fireFrom = fireFrom;
    view->fireFull = fireFull;
    const bool hasMarkers = !view->markers.empty();
    value.bindTo(view->value.assignment());
    min.bindTo(view->min.assignment());
    max.bindTo(view->max.assignment());
    yellowFrom.bindTo(view->yellowFrom.assignment());
    redFrom.bindTo(view->redFrom.assignment());
    maroonFrom.bindTo(view->maroonFrom.assignment());
    const auto dp = [](float v) { return AMetric(v * DIAL_SIZE_SCALE, AMetric::T_DP); };
    view AUI_OVERRIDE_STYLE {
        MinSize { dp(hasMarkers ? 350.f : 200.f), dp(hasMarkers ? 300.f : 200.f) },
        MaxSize { dp(hasMarkers ? 400.f : 300.f), dp(300.f) },
        Expanding {1, 1},
    };
    return Centered { view };
}
