#include <QApplication>
#include <QDirIterator>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QTranslator>

#include "widget/main_window/main_window.h"

namespace {

void applyTheme(QApplication &app, bool dark)
{
    if (!dark) {
        app.setPalette(app.style()->standardPalette());
        return;
    }
    QPalette p;
    p.setColor(QPalette::Window, QColor(53, 53, 53));
    p.setColor(QPalette::WindowText, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(127, 127, 127));
    p.setColor(QPalette::Base, QColor(42, 42, 42));
    p.setColor(QPalette::AlternateBase, QColor(66, 66, 66));
    p.setColor(QPalette::ToolTipBase, Qt::white);
    p.setColor(QPalette::ToolTipText, QColor(53, 53, 53));
    p.setColor(QPalette::Text, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
    p.setColor(QPalette::Dark, QColor(35, 35, 35));
    p.setColor(QPalette::Shadow, QColor(20, 20, 20));
    p.setColor(QPalette::Button, QColor(53, 53, 53));
    p.setColor(QPalette::ButtonText, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(42, 130, 218));
    p.setColor(QPalette::Highlight, QColor(42, 130, 218));
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(127, 127, 127));
    app.setPalette(p);
}

void loadExternalFonts(QApplication &app)
{
    // Fonts live in nested folders (e.g. resource/font/<family>/OTF/...), scan deep.
    QString family;
    QDirIterator it(QCoreApplication::applicationDirPath() + "/resource/font",
                    QStringList() << "*.ttf" << "*.otf", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const int id = QFontDatabase::addApplicationFont(it.next());
        if (id < 0)
            continue;
        const QStringList fams = QFontDatabase::applicationFontFamilies(id);
        if (!fams.isEmpty()) {
            family = fams.first();
            break;
        }
    }
    if (!family.isEmpty())
        app.setFont(QFont(family, 9));
}

void installTranslator(QApplication &app)
{
    QSettings settings;
    if (settings.value("ui/language", "en").toString() != "zh_CN")
        return;
    auto *translator = new QTranslator(&app);
    const QString path =
            QCoreApplication::applicationDirPath() + "/resource/i18n/druppc_zh_CN.qm";
    if (translator->load(path))
        app.installTranslator(translator);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("DRUPPC");
    QApplication::setApplicationName("DRUPPC");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setStyle("Fusion");

    applyTheme(app, QSettings().value("ui/theme", "light").toString() == "dark");
    loadExternalFonts(app);

    const QString iconPath = QCoreApplication::applicationDirPath() + "/resource/icon/main.ico";
    if (QFile::exists(iconPath))
        QApplication::setWindowIcon(QIcon(iconPath));

    MainWindow window;
    QObject::connect(&window, &MainWindow::themeChanged, &app,
                     [&app](bool dark) { applyTheme(app, dark); });
    window.show();
    return app.exec();
}
