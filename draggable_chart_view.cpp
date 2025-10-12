#include <QDateTime>
#include <QDateTimeAxis>
#include <QtCharts/QDateTimeAxis>
#include "log.h"
#include "draggable_chart_view.h"

draggable_chartview::draggable_chartview(QChart *chart, QWidget *parent) : QChartView(chart, parent), dragging_(false), drag_enabled_(false)
{
    setDragMode(QGraphicsView::NoDrag);
    setRubberBand(QChartView::NoRubberBand);
}

void draggable_chartview::set_drag_enabled(bool enabled)
{
    LOG_INFO("dragging has been {}", enabled ? "enabled" : "disabled");
    drag_enabled_ = enabled;
}

void draggable_chartview::mousePressEvent(QMouseEvent *event)
{
    if (drag_enabled_ && event->button() == Qt::LeftButton)
    {
        LOG_WARN("dragging is enabled and left button is pressed");
        dragging_ = true;
        last_mouse_pos_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        emit interaction_started();
    }
    QChartView::mousePressEvent(event);
}

void draggable_chartview::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging_)
    {
        auto axesX = chart()->axes(Qt::Horizontal);
        if (axesX.isEmpty())
        {
            QChartView::mouseMoveEvent(event);
            return;
        }

        auto *axisX = qobject_cast<QDateTimeAxis *>(axesX.first());
        if (axisX == nullptr)
        {
            QChartView::mouseMoveEvent(event);
            return;
        }

        QPoint delta = event->pos() - last_mouse_pos_;
        last_mouse_pos_ = event->pos();

        qint64 currentRange = axisX->max().toMSecsSinceEpoch() - axisX->min().toMSecsSinceEpoch();
        double msPerPixel = static_cast<double>(currentRange) / chart()->plotArea().width();
        qint64 msDelta = -static_cast<qint64>(static_cast<double>(delta.x()) * msPerPixel);

        LOG_TRACE("dragging by {} pixels {} ms", delta.x(), msDelta);

        QDateTime newMin = axisX->min().addMSecs(msDelta);
        QDateTime newMax = axisX->max().addMSecs(msDelta);
        axisX->setRange(newMin, newMax);
    }
    QChartView::mouseMoveEvent(event);
}

void draggable_chartview::mouseReleaseEvent(QMouseEvent *event)
{
    if (dragging_ && event->button() == Qt::LeftButton)
    {
        LOG_WARN("dragging finished");
        dragging_ = false;
        unsetCursor();
        emit view_changed_by_drag();
    }
    QChartView::mouseReleaseEvent(event);
}
