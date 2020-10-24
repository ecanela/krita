/*
 *  Copyright (c) 2020 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "kis_mesh_transform_strategy.h"
#include "tool_transform_args.h"

#include <QPointF>
#include <QPainter>

#include "kis_painting_tweaks.h"
#include "kis_cursor.h"

#include "transform_transaction_properties.h"
#include "KisHandlePainterHelper.h"
#include "kis_transform_utils.h"
#include "kis_signal_compressor.h"


uint qHash(const QPoint &value) {
    return uint((0xffffffffffffffff - quint64(value.y())) ^ quint64(value.x()));
}

struct KisMeshTransformStrategy::Private
{
    Private(KisMeshTransformStrategy *_q,
            const KisCoordinatesConverter *_converter,
            ToolTransformArgs &_currentArgs,
            TransformTransactionProperties &_transaction)
        : q(_q),
          converter(_converter),
          currentArgs(_currentArgs),
          transaction(_transaction),
          recalculateSignalCompressor(40, KisSignalCompressor::FIRST_ACTIVE)
    {
    }

    enum Mode {
        OVER_POINT = 0,
        OVER_SEGMENT,
        OVER_PATCH,
        MULTIPLE_POINT_SELECTION,
        MOVE_MODE,
        ROTATE_MODE,
        SCALE_MODE,
        NOTHING
    };
    Mode mode = NOTHING;

    const KisCoordinatesConverter *converter;
    ToolTransformArgs &currentArgs;
    TransformTransactionProperties &transaction;

    QSet<KisBezierTransformMesh::NodeIndex> selectedNodes;
    boost::optional<KisBezierTransformMesh::SegmentIndex> hoveredSegment;
    boost::optional<KisBezierTransformMesh::ControlPointIndex> hoveredControl;



    boost::optional<qreal> mouseClickSegmentPosition;
    QPointF mouseClickPos;
    bool pointWasDragged = false;
    QPointF lastMousePos;

    KisSignalCompressor recalculateSignalCompressor;

    KisMeshTransformStrategy * const q;

    void recalculateTransformations();
};


KisMeshTransformStrategy::KisMeshTransformStrategy(const KisCoordinatesConverter *converter,
                                                   ToolTransformArgs &currentArgs,
                                                   TransformTransactionProperties &transaction)
    : KisSimplifiedActionPolicyStrategy(converter),
      m_d(new Private(this, converter, currentArgs, transaction))
{

    connect(&m_d->recalculateSignalCompressor, SIGNAL(timeout()),
            SLOT(recalculateTransformations()));

    m_d->selectedNodes << KisBezierTransformMesh::NodeIndex(1, 1);
    m_d->hoveredSegment = KisBezierTransformMesh::SegmentIndex(KisBezierTransformMesh::NodeIndex(0,0), 1);
    m_d->hoveredControl = KisBezierTransformMesh::ControlPointIndex(KisBezierTransformMesh::NodeIndex(1, 0), KisBezierTransformMesh::ControlType::Node);
}

KisMeshTransformStrategy::~KisMeshTransformStrategy()
{
}

void KisMeshTransformStrategy::setTransformFunction(const QPointF &mousePos, bool perspectiveModifierActive)
{
    const qreal grabRadius = KisTransformUtils::effectiveHandleGrabRadius(m_d->converter);

    boost::optional<KisBezierTransformMesh::SegmentIndex> hoveredSegment;
    boost::optional<KisBezierTransformMesh::ControlPointIndex> hoveredControl;
    Private::Mode mode = Private::NOTHING;


    auto controlIt = m_d->currentArgs.meshTransform()->hitTestControlPoint(mousePos, grabRadius);
    if (controlIt != m_d->currentArgs.meshTransform()->endControlPoints()) {
        hoveredControl = controlIt.controlIndex();
        mode = Private::OVER_POINT;
    }

    if (mode == Private::NOTHING) {
        auto nodeIt = m_d->currentArgs.meshTransform()->hitTestNode(mousePos, grabRadius);
        if (nodeIt != m_d->currentArgs.meshTransform()->endControlPoints()) {
            hoveredControl = nodeIt.controlIndex();
            mode = Private::OVER_POINT;
        }
    }

    if (mode == Private::NOTHING) {
        auto segmentIt = m_d->currentArgs.meshTransform()->hitTestSegment(mousePos, grabRadius);
        if (segmentIt != m_d->currentArgs.meshTransform()->endSegments()) {
            hoveredSegment = segmentIt.segmentIndex();
            mode = Private::OVER_SEGMENT;
        }
    }

    if (hoveredControl || hoveredSegment) {
        if (perspectiveModifierActive) {
            mode = Private::MULTIPLE_POINT_SELECTION;
        }
    } else {
        if (m_d->currentArgs.meshTransform()->dstBoundingRect().contains(mousePos)) {
            mode = Private::MOVE_MODE;
        } else if (perspectiveModifierActive) {
            mode = Private::SCALE_MODE;
        } else {
            mode = Private::ROTATE_MODE;
        }
    }

    if (mode != m_d->mode ||
        hoveredControl != m_d->hoveredControl ||
        hoveredSegment != m_d->hoveredSegment) {

        m_d->hoveredControl = hoveredControl;
        m_d->hoveredSegment = hoveredSegment;
        m_d->mode = mode;
        Q_EMIT requestCanvasUpdate();
    }
}

void KisMeshTransformStrategy::paint(QPainter &gc)
{
    gc.save();

    //gc.setOpacity(m_d->transaction.basePreviewOpacity());
    //gc.setTransform(m_d->paintingTransform, true);
    //gc.drawImage(m_d->paintingOffset, m_d->transformedImage);

    gc.restore();

    gc.save();
    gc.setTransform(KisTransformUtils::imageToFlakeTransform(m_d->converter), true);

    KisHandlePainterHelper handlePainter(&gc, 0.5 * KisTransformUtils::handleRadius);

    for (auto it = m_d->currentArgs.meshTransform()->beginSegments();
         it != m_d->currentArgs.meshTransform()->endSegments();
         ++it) {

        if (m_d->hoveredSegment && it.segmentIndex() == *m_d->hoveredSegment) {
            handlePainter.setHandleStyle(KisHandleStyle::highlightedPrimaryHandlesWithSolidOutline());
        } else {
            handlePainter.setHandleStyle(KisHandleStyle::primarySelection());
        }

        QPainterPath path;
        path.moveTo(it.p0());
        path.cubicTo(it.p1(), it.p2(), it.p3());

        handlePainter.drawPath(path);
    }

    for (auto it = m_d->currentArgs.meshTransform()->beginControlPoints();
         it != m_d->currentArgs.meshTransform()->endControlPoints();
         ++it) {

        if (m_d->hoveredControl && *m_d->hoveredControl == it.controlIndex()) {

            handlePainter.setHandleStyle(KisHandleStyle::highlightedPrimaryHandles());

        } else if (it.type() == KisBezierTransformMesh::ControlType::Node &&
                   m_d->selectedNodes.contains(it.nodeIndex())) {

            handlePainter.setHandleStyle(KisHandleStyle::selectedPrimaryHandles());

        } else {
            handlePainter.setHandleStyle(KisHandleStyle::primarySelection());
        }

        if (it.type() == KisBezierTransformMesh::ControlType::Node) {
            handlePainter.drawHandleCircle(*it);
        } else {
            handlePainter.drawConnectionLine(it.node().node, *it);
            handlePainter.drawHandleSmallCircle(*it);
        }
    }

    gc.restore();
}

QCursor KisMeshTransformStrategy::getCurrentCursor() const
{
    QCursor cursor;

    switch (m_d->mode) {
    case Private::OVER_POINT:
        cursor = KisCursor::pointingHandCursor();
        break;
    case Private::OVER_SEGMENT:
        cursor = KisCursor::pointingHandCursor();
        break;
    case Private::OVER_PATCH:
        cursor = KisCursor::pointingHandCursor();
        break;
    case Private::MULTIPLE_POINT_SELECTION:
        cursor = KisCursor::crossCursor();
        break;
    case Private::MOVE_MODE:
        cursor = KisCursor::moveCursor();
        break;
    case Private::ROTATE_MODE:
        cursor = KisCursor::rotateCursor();
        break;
    case Private::SCALE_MODE:
        cursor = KisCursor::sizeVerCursor();
        break;
    case Private::NOTHING:
        cursor = KisCursor::arrowCursor();
        break;
    }

    return cursor;
}

void KisMeshTransformStrategy::externalConfigChanged()
{
    m_d->recalculateTransformations();
}

bool KisMeshTransformStrategy::beginPrimaryAction(const QPointF &pt)
{
    // retval shows if the stroke may have a continuation
    bool retval = false;

    m_d->mouseClickPos = pt;
    m_d->pointWasDragged = false;

    if (m_d->mode == Private::OVER_POINT) {
        KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(m_d->hoveredControl, false);

        m_d->selectedNodes.clear();
        m_d->selectedNodes << m_d->hoveredControl->nodeIndex;
        retval = true;

    } else if (m_d->mode == Private::OVER_SEGMENT) {
        KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(m_d->hoveredSegment, false);

        m_d->selectedNodes.clear();

        auto it = m_d->currentArgs.meshTransform()->find(*m_d->hoveredSegment);
        KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(it != m_d->currentArgs.meshTransform()->endSegments(), false);

        m_d->mouseClickSegmentPosition =
            KisBezierUtils::nearestPoint({it.p0(), it.p1(), it.p2(), it.p3()}, pt);

        m_d->selectedNodes << it.firstNodeIndex() << it.secondNodeIndex();
        retval = true;

    } else if (m_d->mode == Private::MULTIPLE_POINT_SELECTION) {
        if (m_d->hoveredControl) {
            if (!m_d->selectedNodes.contains(m_d->hoveredControl->nodeIndex)) {
                m_d->selectedNodes.insert(m_d->hoveredControl->nodeIndex);
            } else {
                m_d->selectedNodes.remove(m_d->hoveredControl->nodeIndex);
            }
        } else if (m_d->hoveredSegment) {
            auto it = m_d->currentArgs.meshTransform()->find(*m_d->hoveredSegment);
            KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(it != m_d->currentArgs.meshTransform()->endSegments(), false);

            if (!m_d->selectedNodes.contains(it.firstNodeIndex()) ||
                !m_d->selectedNodes.contains(it.secondNodeIndex())) {

                m_d->selectedNodes.insert(it.firstNodeIndex());
                m_d->selectedNodes.insert(it.secondNodeIndex());
            } else {
                m_d->selectedNodes.remove(it.firstNodeIndex());
                m_d->selectedNodes.remove(it.secondNodeIndex());
            }
        }
        retval = false;
    } else if (m_d->mode == Private::MOVE_MODE ||
               m_d->mode == Private::SCALE_MODE ||
               m_d->mode == Private::ROTATE_MODE) {

        retval = true;
    }

    m_d->lastMousePos = pt;
    return retval;
}

void KisMeshTransformStrategy::continuePrimaryAction(const QPointF &pt, bool shiftModifierActve, bool altModifierActive)
{
    if (m_d->mode == Private::OVER_POINT) {
        KIS_SAFE_ASSERT_RECOVER_RETURN(m_d->hoveredControl);

        auto it = m_d->currentArgs.meshTransform()->find(*m_d->hoveredControl);

        if (it.type() == KisBezierTransformMesh::ControlType::Node) {
            it.node().translate(pt - m_d->lastMousePos);
        } else {
            *it += pt - m_d->lastMousePos;
        }
    } else if (m_d->mode == Private::OVER_SEGMENT) {
        KIS_SAFE_ASSERT_RECOVER_RETURN(m_d->hoveredSegment);
        KIS_SAFE_ASSERT_RECOVER_RETURN(m_d->mouseClickSegmentPosition);

        auto it = m_d->currentArgs.meshTransform()->find(*m_d->hoveredSegment);

        // TODO: recover special case for degree-2 curves. There is a special
        //       function for that in KisBezierUtils::interpolateQuadric(), but
        //       it seems like not working properly.

        const QPointF offset = pt - m_d->lastMousePos;

        QPointF offsetP1;
        QPointF offsetP2;

        std::tie(offsetP1, offsetP2) =
            KisBezierUtils::offsetSegment(*m_d->mouseClickSegmentPosition, offset);

        it.p1() += offsetP1;
        it.p2() += offsetP2;
    } else if (m_d->mode == Private::MOVE_MODE) {
        const QPointF offset = pt - m_d->lastMousePos;
        if (m_d->selectedNodes.size() > 1) {
            for (auto it = m_d->selectedNodes.begin(); it != m_d->selectedNodes.end(); ++it) {
                m_d->currentArgs.meshTransform()->node(*it).translate(offset);
            }
        } else {
            m_d->currentArgs.meshTransform()->translate(offset);
        }
    } else if (m_d->mode == Private::SCALE_MODE) {
    } else if (m_d->mode == Private::ROTATE_MODE) {
    }

    m_d->lastMousePos = pt;
}

bool KisMeshTransformStrategy::endPrimaryAction()
{
    m_d->mouseClickSegmentPosition = boost::none;

    return m_d->mode == Private::OVER_POINT ||
        m_d->mode == Private::OVER_SEGMENT ||
        m_d->mode == Private::MOVE_MODE ||
        m_d->mode == Private::SCALE_MODE ||
        m_d->mode == Private::ROTATE_MODE;
}

bool KisMeshTransformStrategy::acceptsClicks() const
{
    return false;
}


void KisMeshTransformStrategy::Private::recalculateTransformations()
{

}

#include "moc_kis_mesh_transform_strategy.cpp"
