﻿/*
 *  SPDX-FileCopyrightText: 2010 Sven Langkamp <sven.langkamp@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_brush_based_paintop_settings.h"

#include <kis_paint_action_type_option.h>
#include <kis_airbrush_option_widget.h>
#include "kis_brush_based_paintop_options_widget.h"
#include <kis_boundary.h>
#include "KisBrushServerProvider.h"
#include <QLineF>
#include "kis_signals_blocker.h"
#include "kis_brush_option.h"
#include <KisPaintopSettingsIds.h>
#include <kis_paintop_preset.h>
#include "KoCanvasResourcesIds.h"
#include "kis_texture_option.h"
#include <KoResourceCacheInterface.h>

struct BrushReader {
    BrushReader(const KisBrushBasedPaintOpSettings *parent)
        : m_parent(parent)
    {
        m_option.readOptionSetting(m_parent, parent->resourcesInterface(), parent->canvasResourcesInterface());
    }

    KisBrushSP brush() {
        return m_option.brush();
    }

    const KisBrushBasedPaintOpSettings *m_parent;
    KisBrushOptionProperties m_option;
};

struct BrushWriter {
    BrushWriter(KisBrushBasedPaintOpSettings *parent)
        : m_parent(parent)
    {
        m_option.readOptionSetting(m_parent, parent->resourcesInterface(), parent->canvasResourcesInterface());
    }

    ~BrushWriter() {
        m_option.writeOptionSetting(m_parent);
    }

    KisBrushSP brush() {
        return m_option.brush();
    }

    KisBrushBasedPaintOpSettings *m_parent;
    KisBrushOptionProperties m_option;
};


KisBrushBasedPaintOpSettings::KisBrushBasedPaintOpSettings(KisResourcesInterfaceSP resourcesInterface)
    : KisOutlineGenerationPolicy<KisPaintOpSettings>(KisCurrentOutlineFetcher::SIZE_OPTION |
                                                     KisCurrentOutlineFetcher::ROTATION_OPTION |
                                                     KisCurrentOutlineFetcher::MIRROR_OPTION |
                                                     KisCurrentOutlineFetcher::SHARPNESS_OPTION,
                                                     resourcesInterface)

{
}

bool KisBrushBasedPaintOpSettings::paintIncremental()
{
    if (hasProperty("PaintOpAction")) {
        return (enumPaintActionType)getInt("PaintOpAction", WASH) == BUILDUP;
    }
    return true;
}

KisPaintOpSettingsSP KisBrushBasedPaintOpSettings::clone() const
{
    KisPaintOpSettingsSP _settings = KisOutlineGenerationPolicy<KisPaintOpSettings>::clone();
    KisBrushBasedPaintOpSettingsSP settings = dynamic_cast<KisBrushBasedPaintOpSettings*>(_settings.data());

    /**
     * We don't copy m_savedBrush directly because it can also
     * depend on "canvas resources", which should be baked into
     * the preset on cloning. This dependency is tracked by
     * KisPresetShadowUpdater and reset when needed via
     * resetting cache interface.
     */
    settings->setResourceCacheInterface(resourceCacheInterface());

    return settings;
}

KisBrushSP KisBrushBasedPaintOpSettings::brush() const
{
    KisBrushSP brush = m_savedBrush;

    if (!brush) {
        BrushReader w(this);
        brush = w.brush();
        m_savedBrush = brush;
    }

    return brush;
}

QPainterPath KisBrushBasedPaintOpSettings::brushOutlineImpl(const KisPaintInformation &info,
                                                            const OutlineMode &mode,
                                                            qreal alignForZoom,
                                                            qreal additionalScale)
{
    QPainterPath path;

    if (mode.isVisible) {
        KisBrushSP brush = this->brush();
        if (!brush) return path;
        qreal finalScale = brush->scale() * additionalScale;

        QPainterPath realOutline = brush->outline();

        if (mode.forceCircle) {

            QPainterPath ellipse;
            ellipse.addEllipse(realOutline.boundingRect());
            realOutline = ellipse;
        }

        path = outlineFetcher()->fetchOutline(info, this, realOutline, mode, alignForZoom, finalScale, brush->angle());

        if (mode.showTiltDecoration) {
            const QPainterPath tiltLine = makeTiltIndicator(info,
                realOutline.boundingRect().center(),
                realOutline.boundingRect().width() * 0.5,
                3.0);
            path.addPath(outlineFetcher()->fetchOutline(info, this, tiltLine, mode, alignForZoom, finalScale, 0.0, true, realOutline.boundingRect().center().x(), realOutline.boundingRect().center().y()));
        }
    }

    return path;
}

QPainterPath KisBrushBasedPaintOpSettings::brushOutline(const KisPaintInformation &info, const OutlineMode &mode, qreal alignForZoom)
{
    return brushOutlineImpl(info, mode, alignForZoom, 1.0);
}

void KisBrushBasedPaintOpSettings::setAngle(qreal value)
{
    BrushWriter w(this);
    if (!w.brush()) return;
    w.brush()->setAngle(value);
}

qreal KisBrushBasedPaintOpSettings::angle()
{
    return this->brush()->angle();
}

void KisBrushBasedPaintOpSettings::setSpacing(qreal value)
{
    BrushWriter w(this);
    if (!w.brush()) return;
    w.brush()->setSpacing(value);
}

qreal KisBrushBasedPaintOpSettings::spacing()
{
    return this->brush()->spacing();
}

void KisBrushBasedPaintOpSettings::setAutoSpacing(bool active, qreal coeff)
{
    BrushWriter w(this);
    if (!w.brush()) return;
    w.brush()->setAutoSpacing(active, coeff);
}


bool KisBrushBasedPaintOpSettings::autoSpacingActive()
{
    return this->brush()->autoSpacingActive();
}

qreal KisBrushBasedPaintOpSettings::autoSpacingCoeff()
{
    return this->brush()->autoSpacingCoeff();
}

void KisBrushBasedPaintOpSettings::setPaintOpSize(qreal value)
{
    BrushWriter w(this);
    if (!w.brush()) return;

    w.brush()->setUserEffectiveSize(value);
}

qreal KisBrushBasedPaintOpSettings::paintOpSize() const
{
    return this->brush()->userEffectiveSize();
}



#include <brushengine/kis_slider_based_paintop_property.h>
#include "kis_paintop_preset.h"
#include "KisPaintOpPresetUpdateProxy.h"

QList<KisUniformPaintOpPropertySP> KisBrushBasedPaintOpSettings::uniformProperties(KisPaintOpSettingsSP settings, QPointer<KisPaintOpPresetUpdateProxy> updateProxy)
{
    QList<KisUniformPaintOpPropertySP> props =
        listWeakToStrong(m_uniformProperties);

    if (props.isEmpty()) {
        {
            KisIntSliderBasedPaintOpPropertyCallback *prop =
                new KisIntSliderBasedPaintOpPropertyCallback(
                    KisIntSliderBasedPaintOpPropertyCallback::Int,
                    KisIntSliderBasedPaintOpPropertyCallback::SubType_Angle,
                    "angle",
                    i18n("Angle"),
                    settings, 0);

            prop->setRange(0, 360);

            prop->setReadCallback(
                [](KisUniformPaintOpProperty *prop) {
                    KisBrushBasedPaintOpSettings *s =
                        dynamic_cast<KisBrushBasedPaintOpSettings*>(prop->settings().data());

                    const qreal angleResult = kisRadiansToDegrees(s->angle());
                    prop->setValue(angleResult);
                });
            prop->setWriteCallback(
                [](KisUniformPaintOpProperty *prop) {
                    KisBrushBasedPaintOpSettings *s =
                        dynamic_cast<KisBrushBasedPaintOpSettings*>(prop->settings().data());

                    s->setAngle(kisDegreesToRadians(prop->value().toReal()));
                });

            QObject::connect(updateProxy, SIGNAL(sigSettingsChanged()), prop, SLOT(requestReadValue()));
            prop->requestReadValue();
            props << toQShared(prop);
        }
        {
            KisUniformPaintOpPropertyCallback *prop =
                new KisUniformPaintOpPropertyCallback(
                    KisUniformPaintOpPropertyCallback::Bool,
                    "auto_spacing",
                    i18n("Auto Spacing"),
                    settings, 0);

            prop->setReadCallback(
                [](KisUniformPaintOpProperty *prop) {
                    KisBrushBasedPaintOpSettings *s =
                        dynamic_cast<KisBrushBasedPaintOpSettings*>(prop->settings().data());

                    prop->setValue(s->autoSpacingActive());
                });
            prop->setWriteCallback(
                [](KisUniformPaintOpProperty *prop) {
                    KisBrushBasedPaintOpSettings *s =
                        dynamic_cast<KisBrushBasedPaintOpSettings*>(prop->settings().data());

                    s->setAutoSpacing(prop->value().toBool(), s->autoSpacingCoeff());
                });

            QObject::connect(updateProxy, SIGNAL(sigSettingsChanged()), prop, SLOT(requestReadValue()));
            prop->requestReadValue();
            props << toQShared(prop);
        }

        {
            KisDoubleSliderBasedPaintOpPropertyCallback *prop =
                new KisDoubleSliderBasedPaintOpPropertyCallback(
                    KisDoubleSliderBasedPaintOpPropertyCallback::Double,
                    "spacing",
                    i18n("Spacing"),
                    settings, 0);

            prop->setRange(0.01, 10);
            prop->setSingleStep(0.01);
            prop->setExponentRatio(3.0);


            prop->setReadCallback(
                [](KisUniformPaintOpProperty *prop) {
                    KisBrushBasedPaintOpSettings *s =
                        dynamic_cast<KisBrushBasedPaintOpSettings*>(prop->settings().data());
                    if (s) {
                        const qreal value = s->autoSpacingActive() ?
                            s->autoSpacingCoeff() : s->spacing();
                        prop->setValue(value);
                    }
                });
            prop->setWriteCallback(
                [](KisUniformPaintOpProperty *prop) {
                    KisBrushBasedPaintOpSettings *s =
                        dynamic_cast<KisBrushBasedPaintOpSettings*>(prop->settings().data());
                    if (s) {
                        if (s->autoSpacingActive()) {
                            s->setAutoSpacing(true, prop->value().toReal());
                        } else {
                            s->setSpacing(prop->value().toReal());
                        }
                    }
                });

            QObject::connect(updateProxy, SIGNAL(sigSettingsChanged()), prop, SLOT(requestReadValue()));
            prop->requestReadValue();
            props << toQShared(prop);
        }
    }

    return KisPaintOpSettings::uniformProperties(settings, updateProxy) + props;
}

void KisBrushBasedPaintOpSettings::onPropertyChanged()
{
    m_savedBrush.clear();
    KisOutlineGenerationPolicy<KisPaintOpSettings>::onPropertyChanged();
}

bool KisBrushBasedPaintOpSettings::hasPatternSettings() const
{
    return true;
}

QList<int> KisBrushBasedPaintOpSettings::requiredCanvasResources() const
{
    QList<int> result;

    if (brush()->applyingGradient() || KisTextureProperties::applyingGradient(this)) {
        result << KoCanvasResource::CurrentGradient;
        result << KoCanvasResource::ForegroundColor;
        result << KoCanvasResource::BackgroundColor;
    }

    return result;
}

void KisBrushBasedPaintOpSettings::setResourceCacheInterface(KoResourceCacheInterfaceSP cacheInterface)
{
    m_savedBrush.clear();

    QVariant brush = cacheInterface ? cacheInterface->fetch("settings/brush") : QVariant();

    if (brush.isValid()) {
        KisBrushSP brushPointer = brush.value<KisBrushSP>();
        KIS_SAFE_ASSERT_RECOVER_NOOP(brushPointer);

        if (brushPointer) {
            m_savedBrush = brushPointer->clone().dynamicCast<KisBrush>();
        }
    }

    KisOutlineGenerationPolicy<KisPaintOpSettings>::setResourceCacheInterface(cacheInterface);
}

void KisBrushBasedPaintOpSettings::regenerateResourceCache(KoResourceCacheInterfaceSP cacheInterface)
{
    KisOutlineGenerationPolicy<KisPaintOpSettings>::regenerateResourceCache(cacheInterface);

    KisBrushSP brush = this->brush();
    KIS_SAFE_ASSERT_RECOVER_RETURN(brush);

    brush->coldInitBrush();

    cacheInterface->put("settings/brush", QVariant::fromValue(brush->clone().dynamicCast<KisBrush>()));
}
