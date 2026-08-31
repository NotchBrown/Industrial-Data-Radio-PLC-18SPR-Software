#ifndef DRUPPC_MAIN_WINDOW_H
#define DRUPPC_MAIN_WINDOW_H

#include <QActionGroup>
#include <QLabel>
#include <QMainWindow>

class QProgressBar;
class QTranslator;

namespace Ui {
class MainWindow;
}
class ConfigPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Switch UI language at runtime ("en" / "zh_CN"), no restart needed.
    void applyLanguage(const QString &lang);

signals:
    void themeChanged(bool dark);

private slots:
    void onNewMaster();
    void onNewSlave();
    void onOpen();
    void onSave();
    void onSaveAs();
    void onCloseTab();
    void onExit();
    void onAbout();
    void onConnectionSettings();
    void onTabChanged(int index);
    void onTabCloseRequested(int index);

private:
    ConfigPage *currentPage() const;
    void openPath(const QString &path);
    void populateExamples();
    void connectPage(ConfigPage *page);
    void addConfigPage(ConfigPage *page, const QString &docName = QString());
    void saveTo(ConfigPage *page, const QString &path);
    void updateTabTitle(ConfigPage *page);
    void initStatusbar();
    void initActionGroups();
    void applyRefreshToAll();
    void retranslate();

    Ui::MainWindow *ui;
    QLabel *m_labelLink = nullptr;
    QLabel *m_labelCounters = nullptr;
    QLabel *m_labelMessage = nullptr;
    QProgressBar *m_busyBar = nullptr;
    QActionGroup *m_refreshGroup = nullptr;
    QTranslator *m_translator = nullptr;
    int m_untitledCounter = 0;
    int m_refreshMs = 0;
};

#endif // DRUPPC_MAIN_WINDOW_H
