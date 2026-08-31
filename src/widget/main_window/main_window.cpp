#include "widget/main_window/main_window.h"
#include "ui_main_window.h"

#include "widget/about/about.h"
#include "widget/config_page/config_page.h"
#include "widget/config_page/master_config_page.h"
#include "widget/config_page/slave_config_page.h"
#include "widget/connection_settings/connection_settings.h"

#include <QActionGroup>
#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressBar>
#include <QSettings>
#include <QTranslator>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    initStatusbar();
    initActionGroups();

    connect(ui->actionNewMaster, &QAction::triggered, this, &MainWindow::onNewMaster);
    connect(ui->actionNewSlave, &QAction::triggered, this, &MainWindow::onNewSlave);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::onOpen);
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::onSave);
    connect(ui->actionSaveAs, &QAction::triggered, this, &MainWindow::onSaveAs);
    connect(ui->actionCloseTab, &QAction::triggered, this, &MainWindow::onCloseTab);
    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::onExit);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onAbout);
    connect(ui->actionConnectionSettings, &QAction::triggered, this,
            &MainWindow::onConnectionSettings);

    // Device > bulk operations apply to the current configuration's board.
    connect(ui->actionReadAll, &QAction::triggered, this, [this] {
        if (ConfigPage *page = currentPage())
            page->readAll();
    });
    connect(ui->actionWriteAll, &QAction::triggered, this, [this] {
        if (ConfigPage *page = currentPage())
            page->writeAll();
    });
    connect(ui->actionApplyAll, &QAction::triggered, this, [this] {
        if (ConfigPage *page = currentPage())
            page->applyAll();
    });

    connect(ui->actionLangEnglish, &QAction::triggered, this, [this] {
        QSettings s;
        s.setValue("ui/language", "en");
        applyLanguage(QStringLiteral("en"));
    });
    connect(ui->actionLangChinese, &QAction::triggered, this, [this] {
        QSettings s;
        s.setValue("ui/language", "zh_CN");
        applyLanguage(QStringLiteral("zh_CN"));
    });
    connect(ui->actionThemeLight, &QAction::triggered, this, [this] {
        QSettings s;
        s.setValue("ui/theme", "light");
        emit themeChanged(false);
    });
    connect(ui->actionThemeDark, &QAction::triggered, this, [this] {
        QSettings s;
        s.setValue("ui/theme", "dark");
        emit themeChanged(true);
    });

    connect(ui->tabConfigs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(ui->tabConfigs, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);

    QSettings s;
    const QString lang = s.value("ui/language", "en").toString();
    (lang == "zh_CN" ? ui->actionLangChinese : ui->actionLangEnglish)->setChecked(true);
    (s.value("ui/theme", "light").toString() == "dark" ? ui->actionThemeDark
                                                       : ui->actionThemeLight)
            ->setChecked(true);

    applyLanguage(lang); // install the startup translation before building tabs
    populateExamples();  // File > Example Files from resource/demo/
    onNewMaster(); // start with one master configuration tab
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::initStatusbar()
{
    m_labelLink = new QLabel(this);
    m_labelCounters = new QLabel(this);
    m_labelMessage = new QLabel(this);
    // Status bar text stays English by design (user requirement); not translated.
    m_labelLink->setText(QStringLiteral("Port: Not connected"));
    m_labelCounters->setText(QStringLiteral("TX: 0  RX: 0"));
    m_labelMessage->setText(QStringLiteral("Ready"));
    ui->statusbar->addPermanentWidget(m_labelLink);
    ui->statusbar->addPermanentWidget(m_labelCounters);
    ui->statusbar->addPermanentWidget(m_labelMessage, 1);

    // Indeterminate busy indicator, far right; visible while the current
    // tab's request queue is draining.
    m_busyBar = new QProgressBar(this);
    m_busyBar->setRange(0, 0);
    m_busyBar->setTextVisible(false);
    m_busyBar->setFixedWidth(120);
    m_busyBar->setFixedHeight(14);
    m_busyBar->hide();
    ui->statusbar->addPermanentWidget(m_busyBar);
}

void MainWindow::initActionGroups()
{
    auto *langGroup = new QActionGroup(this);
    langGroup->addAction(ui->actionLangEnglish);
    langGroup->addAction(ui->actionLangChinese);
    langGroup->setExclusive(true);

    auto *themeGroup = new QActionGroup(this);
    themeGroup->addAction(ui->actionThemeLight);
    themeGroup->addAction(ui->actionThemeDark);
    themeGroup->setExclusive(true);

    m_refreshGroup = new QActionGroup(this);
    m_refreshGroup->addAction(ui->actionRefreshOff);
    m_refreshGroup->addAction(ui->actionRefresh1s);
    m_refreshGroup->addAction(ui->actionRefresh2s);
    m_refreshGroup->addAction(ui->actionRefresh5s);
    m_refreshGroup->addAction(ui->actionRefresh10s);
    m_refreshGroup->setExclusive(true);

    const auto setRefresh = [this](int ms) {
        m_refreshMs = ms;
        QSettings().setValue("refresh/intervalMs", ms);
        applyRefreshToAll();
    };
    connect(ui->actionRefreshOff, &QAction::triggered, this, [setRefresh] { setRefresh(0); });
    connect(ui->actionRefresh1s, &QAction::triggered, this, [setRefresh] { setRefresh(1000); });
    connect(ui->actionRefresh2s, &QAction::triggered, this, [setRefresh] { setRefresh(2000); });
    connect(ui->actionRefresh5s, &QAction::triggered, this, [setRefresh] { setRefresh(5000); });
    connect(ui->actionRefresh10s, &QAction::triggered, this, [setRefresh] { setRefresh(10000); });

    // Restore the stored interval and check the matching menu item.
    const int saved = QSettings().value("refresh/intervalMs", 0).toInt();
    m_refreshMs = saved;
    QAction *checked = ui->actionRefreshOff;
    if (saved == 1000)
        checked = ui->actionRefresh1s;
    else if (saved == 2000)
        checked = ui->actionRefresh2s;
    else if (saved == 5000)
        checked = ui->actionRefresh5s;
    else if (saved == 10000)
        checked = ui->actionRefresh10s;
    checked->setChecked(true);
}

void MainWindow::applyRefreshToAll()
{
    for (int i = 0; i < ui->tabConfigs->count(); ++i) {
        if (auto *page = qobject_cast<ConfigPage *>(ui->tabConfigs->widget(i)))
            page->setAutoRefreshInterval(m_refreshMs);
    }
}

void MainWindow::applyLanguage(const QString &lang)
{
    if (m_translator) {
        qApp->removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }
    if (lang == QLatin1String("zh_CN")) {
        auto *t = new QTranslator(this);
        const QString path = QCoreApplication::applicationDirPath()
                + QStringLiteral("/resource/i18n/druppc_zh_CN.qm");
        if (t->load(path)) {
            m_translator = t;
            qApp->installTranslator(m_translator);
        } else {
            delete t;
        }
    }
    retranslate();
}

void MainWindow::retranslate()
{
    ui->retranslateUi(this);
    for (int i = 0; i < ui->tabConfigs->count(); ++i) {
        if (auto *page = qobject_cast<ConfigPage *>(ui->tabConfigs->widget(i)))
            page->retranslate();
    }
    if (ConfigPage *page = currentPage())
        m_labelLink->setText(page->connectionText());
    else
        m_labelLink->setText(QStringLiteral("Port: Not connected"));
    m_labelCounters->setText(QStringLiteral("TX: 0  RX: 0"));
    m_labelMessage->setText(QStringLiteral("Ready"));
}

ConfigPage *MainWindow::currentPage() const
{
    return qobject_cast<ConfigPage *>(ui->tabConfigs->currentWidget());
}

void MainWindow::connectPage(ConfigPage *page)
{
    connect(page, &ConfigPage::statusMessage, this, [this, page](const QString &msg) {
        if (ui->tabConfigs->currentWidget() == page)
            m_labelMessage->setText(msg);
    });
    connect(page, &ConfigPage::countersChanged, this, [this, page](qint64 tx, qint64 rx) {
        if (ui->tabConfigs->currentWidget() == page)
            m_labelCounters->setText(
                    QStringLiteral("TX: %1  RX: %2").arg(tx).arg(rx));
    });
    connect(page, &ConfigPage::connectionChanged, this, [this, page](bool) {
        if (ui->tabConfigs->currentWidget() == page)
            m_labelLink->setText(page->connectionText());
    });
    connect(page, &ConfigPage::busyChanged, this, [this, page](bool busy) {
        if (ui->tabConfigs->currentWidget() == page)
            m_busyBar->setVisible(busy);
    });
}

void MainWindow::addConfigPage(ConfigPage *page, const QString &docName)
{
    if (docName.isEmpty()) {
        page->setProperty("docName", tr("Untitled %1").arg(++m_untitledCounter));
    } else {
        page->setProperty("docName", docName);
    }
    connectPage(page);
    page->setAutoRefreshInterval(m_refreshMs);
    const int idx = ui->tabConfigs->addTab(page, QString());
    updateTabTitle(page);
    ui->tabConfigs->setCurrentIndex(idx);
}

void MainWindow::updateTabTitle(ConfigPage *page)
{
    const int idx = ui->tabConfigs->indexOf(page);
    if (idx < 0)
        return;
    const QString docName = page->filePath().isEmpty()
            ? page->property("docName").toString()
            : QFileInfo(page->filePath()).fileName();
    ui->tabConfigs->setTabText(idx, QStringLiteral("[%1] %2").arg(
                                            page->isMaster() ? QStringLiteral("Master")
                                                             : QStringLiteral("Slave"),
                                            docName));
}

void MainWindow::onNewMaster()
{
    addConfigPage(new MasterConfigPage(this));
}

void MainWindow::onNewSlave()
{
    addConfigPage(new SlaveConfigPage(this));
}

void MainWindow::onOpen()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Configuration"), QString(),
                                                      tr("Data Radio Config (*.iml)"));
    if (!path.isEmpty())
        openPath(path);
}

void MainWindow::openPath(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Open"), tr("Cannot read %1").arg(path));
        return;
    }
    QDomDocument doc;
    QString err;
    int line = 0;
    int col = 0;
    if (!doc.setContent(&f, &err, &line, &col)) {
        f.close();
        QMessageBox::warning(this, tr("Open"),
                             tr("Invalid configuration file: %1 (line %2)").arg(err).arg(line));
        return;
    }
    f.close();

    const QString type = doc.firstChildElement(QStringLiteral("druppc"))
                                 .attribute(QStringLiteral("type"));
    ConfigPage *page = nullptr;
    if (type == QStringLiteral("master"))
        page = new MasterConfigPage(this);
    else if (type == QStringLiteral("slave"))
        page = new SlaveConfigPage(this);
    if (!page) {
        QMessageBox::warning(this, tr("Open"), tr("Unknown configuration type."));
        return;
    }
    if (!page->fromXml(doc)) {
        delete page;
        QMessageBox::warning(this, tr("Open"), tr("Configuration type mismatch."));
        return;
    }
    page->setFilePath(path);
    addConfigPage(page, QFileInfo(path).fileName());
}

// Populate File > Example Files from every .iml shipped in resource/demo/.
void MainWindow::populateExamples()
{
    const QDir demoDir(QCoreApplication::applicationDirPath()
                       + QStringLiteral("/resource/demo"));
    if (!demoDir.exists())
        return;
    const QStringList files =
            demoDir.entryList(QStringList(QStringLiteral("*.iml")), QDir::Files);
    for (const QString &name : files) {
        QAction *a = ui->menuExamples->addAction(name);
        const QString full = demoDir.filePath(name);
        connect(a, &QAction::triggered, this, [this, full] { openPath(full); });
    }
    ui->menuExamples->setEnabled(!files.isEmpty());
}

void MainWindow::onSave()
{
    ConfigPage *page = currentPage();
    if (!page)
        return;
    if (page->filePath().isEmpty()) {
        onSaveAs();
        return;
    }
    saveTo(page, page->filePath());
}

void MainWindow::onSaveAs()
{
    ConfigPage *page = currentPage();
    if (!page)
        return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Configuration"),
                                                      page->filePath(),
                                                      tr("Data Radio Config (*.iml)"));
    if (path.isEmpty())
        return;
    page->setFilePath(path);
    saveTo(page, path);
}

void MainWindow::saveTo(ConfigPage *page, const QString &path)
{
    QDomDocument doc;
    page->toXml(doc);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save"), tr("Cannot write %1").arg(path));
        return;
    }
    f.write(doc.toByteArray(2));
    f.close();
    updateTabTitle(page);
    m_labelMessage->setText(tr("Configuration saved: %1").arg(path));
}

void MainWindow::onCloseTab()
{
    const int idx = ui->tabConfigs->currentIndex();
    if (idx >= 0)
        onTabCloseRequested(idx);
}

void MainWindow::onExit()
{
    close();
}

void MainWindow::onAbout()
{
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::onConnectionSettings()
{
    ConnectionSettingsDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    for (int i = 0; i < ui->tabConfigs->count(); ++i) {
        if (auto *page = qobject_cast<ConfigPage *>(ui->tabConfigs->widget(i)))
            page->applyConnectionSettings();
    }
    m_labelMessage->setText(tr("Connection settings updated."));
}

void MainWindow::onTabChanged(int index)
{
    Q_UNUSED(index)
    if (ConfigPage *page = currentPage()) {
        m_labelLink->setText(page->connectionText());
        m_busyBar->setVisible(page->isBusy());
    }
}

void MainWindow::onTabCloseRequested(int index)
{
    QWidget *w = ui->tabConfigs->widget(index);
    ui->tabConfigs->removeTab(index);
    w->deleteLater();
}
