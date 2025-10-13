#include <QApplication>
#include <QDir>
#include <QPainter>
#include <QIcon>
#include "log.h"
#include "main_window.h"
#include "scoped_exit.h"

static QIcon emoji_to_icon(const QString& emoji, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    auto font = QApplication::font();
    font.setPointSizeF(size * 0.6);
    painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, emoji);
    painter.end();
    return pixmap;
}

int main(int argc, char* argv[])
{
    std::string app_name(argv[0]);
    init_log(app_name + ".log");
    set_level("trace");
    DEFER(shutdown_log());

    QApplication app(argc, argv);

    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(emoji_to_icon("💧", 64));

    main_window main_window;
    main_window.show();

    return QApplication::exec();
}
