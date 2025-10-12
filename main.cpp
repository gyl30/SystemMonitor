#include <QApplication>
#include <QDir>
#include "log.h"
#include "main_window.h"
#include "scoped_exit.h"

int main(int argc, char* argv[])
{
    std::string app_name(argv[0]);
    init_log(app_name + ".log");
    set_level("trace");
    DEFER(shutdown_log());

    QApplication app(argc, argv);

    main_window main_window;
    main_window.show();

    return QApplication::exec();
}
