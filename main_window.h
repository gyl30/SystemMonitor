#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QTimer>
#include <QMenu>
#include <QDateTime>
#include <QMainWindow>
#include <QCloseEvent>
#include <QSystemTrayIcon>
#include <QGraphicsSimpleTextItem>

#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLegendMarker>

#include <memory>

#include "database_manager.h"
#include "draggable_chart_view.h"

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

   protected:
    void closeEvent(QCloseEvent* event) override;

   private slots:
    void perform_update_tick();
    void handle_series_hovered(const QPointF& point, bool state);
    void toggle_series_visibility(const QString& name);
    void snap_back_to_live_view();
    void process_new_interfaces();
    void onInteractionStarted();
    void onInteractionFinished();
    void on_tray_icon_activated(QSystemTrayIcon::ActivationReason reason);
    static void quit_application();

   private:
    void setup_chart();
    void add_series_for_interface(const QString& interface_name);
    void update_x_axis(const QDateTime& start, const QDateTime& end);
    void rescale_y_axis();
    void update_all_visuals();
    void load_data_for_display(const QDateTime& start, const QDateTime& end);
    void setup_tray_icon();

    QChart* chart_ = nullptr;
    draggable_chartview* chart_view_ = nullptr;

    QDateTimeAxis* axis_x_ = nullptr;
    QValueAxis* axis_y_ = nullptr;

    QMap<QString, interface_series> series_map_;
    QList<QColor> color_palette_;
    int color_index_ = 0;

    QGraphicsSimpleTextItem* tooltip_ = nullptr;
    QDateTime first_timestamp_;
    QString isolated_interface_name_;

    QTimer* collection_timer_ = nullptr;
    QTimer* snap_back_timer_ = nullptr;
    bool is_manual_view_active_ = false;

    QStringList new_interfaces_queue_;

    std::unique_ptr<database_manager> db_manager_;

    QSystemTrayIcon* tray_icon_ = nullptr;
    QMenu* tray_menu_ = nullptr;
    QAction* show_hide_action_ = nullptr;
    QAction* quit_action_ = nullptr;
};

#endif
