#ifndef DRAGGABLE_CHART_VIEW_H
#define DRAGGABLE_CHART_VIEW_H

#include <QtCharts/QChartView>
#include <QMouseEvent>

class draggable_chartview : public QChartView
{
    Q_OBJECT

   public:
    explicit draggable_chartview(QChart *chart, QWidget *parent = nullptr);

    void set_drag_enabled(bool enabled);

   signals:
    void interaction_started();

    void view_changed_by_drag();

   protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

   private:
    bool dragging_;
    bool drag_enabled_;
    QPoint last_mouse_pos_;
};

#endif
