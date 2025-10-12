#include <QApplication>
#include <QDir>
#include "log.h"
#include "main_window.h"
#include "scoped_exit.h"
#include "database_manager.h"

int main(int argc, char* argv[])
{
    std::string app_name(argv[0]);
    init_log(app_name + ".log");
    set_level("trace");
    DEFER(shutdown_log());

    QApplication app(argc, argv);
    
    QString db_path = QDir(app.applicationDirPath()).filePath("network_speed.db");
    database_manager db_manager(db_path);
    db_manager.prune_old_data(30);

    main_window main_window;
    main_window.set_database_manager(&db_manager);
    main_window.show();
    
    return app.exec();
}
