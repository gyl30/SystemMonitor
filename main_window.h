#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QDateTime>
#include <QGraphicsSimpleTextItem>
#include <QTimer>
#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLegendMarker>
#include "database_manager.h"
#include "draggable_chart_view.h"    // MODIFICATION
#include <memory>

QT_USE_NAMESPACE

struct interface_series
{
    QLineSeries* upload = nullptr;
    QLineSeries* download = nullptr;
    QLegendMarker* marker = nullptr;
};

class main_window : public QMainWindow
{
    Q_OBJECT

   public:
    explicit main_window(QWidget* parent = nullptr);
    ~main_window() override;

   private slots:
    void perform_update_tick();
    void handle_series_hovered(const QPointF& point, bool state);
    void toggle_series_visibility(const QString& name);
    void snap_back_to_live_view();
    void process_new_interfaces();
    void onInteractionStarted();     // MODIFICATION: Slot for drag start
    void onInteractionFinished();    // MODIFICATION: Slot for drag end

   private:
    void setup_chart();
    void add_series_for_interface(const QString& interface_name);
    void update_x_axis(const QDateTime& start, const QDateTime& end);
    void rescale_y_axis();
    void update_all_visuals();
    void load_data_for_display(const QDateTime& start, const QDateTime& end);

    QChart* chart_;
    draggable_chartview* chart_view_;    // MODIFICATION

    QDateTimeAxis* axis_x_;
    QValueAxis* axis_y_;

    QMap<QString, interface_series> series_map_;
    QList<QColor> color_palette_;
    int color_index_;

    QGraphicsSimpleTextItem* tooltip_;
    QDateTime first_timestamp_;
    QString isolated_interface_name_;

    QTimer* collection_timer_;
    QTimer* snap_back_timer_;
    bool is_manual_view_active_;

    QStringList new_interfaces_queue_;

    std::unique_ptr<database_manager> db_manager_;
};

#endif
