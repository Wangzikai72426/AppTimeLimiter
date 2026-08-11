#include "MainWindow.h"
#include "SettingsDialog.h"
#include "GroupLimitDialog.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QStyle>
#include <QSettings>
#include <QList>
#include <QPair>
#include <algorithm>
#include <QDebug>
#include <QTime>
#include <QIcon>
#include <QDateTime>
#include <QListWidget>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QScreen>
#include <QTimer>
#include <QPainter>
#include <QLabel>
#include <QFont>
#include <functional>

// ==================== 自定义 Toast 提示控件（黑底白字，屏幕顶部中央滑入）====================
class ToastWidget : public QWidget
{
public:
    explicit ToastWidget(const QString& title, const QString& message, QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                       | Qt::ToolTip | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_ShowWithoutActivating);

        setObjectName("toastRoot");
        setStyleSheet(
            "#toastRoot { background-color: #1a1a1a; border: 1px solid #3a3a3a; }"
            "QLabel { color: #ffffff; background: transparent; }");

        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 10, 16, 10);
        layout->setSpacing(4);

        QLabel* titleLabel = new QLabel(title, this);
        QFont tf = titleLabel->font();
        tf.setBold(true);
        tf.setPointSize(10);
        titleLabel->setFont(tf);

        QLabel* msgLabel = new QLabel(message, this);
        QFont mf = msgLabel->font();
        mf.setPointSize(9);
        msgLabel->setFont(mf);
        msgLabel->setWordWrap(true);

        layout->addWidget(titleLabel);
        layout->addWidget(msgLabel);

        setFixedWidth(300);
        adjustSize();
    }

    // 关闭回调（由 MainWindow 设置，用于从堆叠列表移除并重排）
    std::function<void(ToastWidget*)> onClosed;

    void slideIn(const QPoint& targetPos)
    {
        QPoint startPos(targetPos.x(), targetPos.y() - height() - 30);
        move(startPos);
        show();
        QPropertyAnimation* anim = new QPropertyAnimation(this, "pos", this);
        anim->setDuration(280);
        anim->setStartValue(startPos);
        anim->setEndValue(targetPos);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    void moveToPos(const QPoint& targetPos)
    {
        QPropertyAnimation* anim = new QPropertyAnimation(this, "pos", this);
        anim->setDuration(200);
        anim->setStartValue(pos());
        anim->setEndValue(targetPos);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    void slideOut()
    {
        QPropertyAnimation* anim = new QPropertyAnimation(this, "pos", this);
        anim->setDuration(260);
        anim->setStartValue(pos());
        anim->setEndValue(QPoint(pos().x(), pos().y() - height() - 30));
        anim->setEasingCurve(QEasingCurve::InCubic);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            hide();
            if (onClosed) onClosed(this);
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_isLocked(false)
    , m_appLimitDialog(nullptr)
    , m_groupLimitDialog(nullptr)
    , m_expanded(false)
    , m_expandAnim(nullptr)
    , m_trayBlinkTimer(nullptr)
    , m_trayBlinkState(false)
{
    m_monitor = new AppMonitor(this);
    m_lockScreen = new LockScreen();
    //start
    initUI();
    initTray();

    // Connect monitor signals
    connect(m_monitor, &AppMonitor::appUsageUpdated, this, &MainWindow::onAppUsageUpdated);
    connect(m_monitor, &AppMonitor::totalUsageUpdated, this, &MainWindow::onTotalUsageUpdated);
    connect(m_monitor, &AppMonitor::appOpened, this, &MainWindow::onAppOpened);
    connect(m_monitor, &AppMonitor::appLimitReached, this, &MainWindow::onAppLimitReached);
    connect(m_monitor, &AppMonitor::totalLimitReached, this, &MainWindow::onTotalLimitReached);
    connect(m_monitor, &AppMonitor::groupLimitReached, this, &MainWindow::onGroupLimitReached);

    // Connect lock screen unlock signal
    connect(m_lockScreen, &LockScreen::unlocked, this, &MainWindow::onLockScreenUnlocked);
    // Start monitoring
    m_monitor->start();

    // Initial UI refresh
    refreshUsageTable();
    refreshLimitTable();
    refreshGroupTable();
    updateStatusLabels();

    setWindowTitle("健康使用电脑");
    resize(800, 620);

    QTimer* m_refreshLabels = new QTimer(this);
    QObject::connect(m_refreshLabels, &QTimer::timeout, [=]() {
        refreshUsageTable();
        refreshLimitTable();
        refreshGroupTable();
        updateStatusLabels();
        });
    m_refreshLabels->start(1000);

}

MainWindow::~MainWindow()
{
    delete m_lockScreen;
}

// ====================== Tool Function ======================
// 在 MainWindow.cpp 添加私有辅助函数
QStringList MainWindow::getHiddenApps() const
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    return settings.value("HiddenApps").toStringList();
}

void MainWindow::setHiddenApps(const QStringList& list)
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    settings.setValue("HiddenApps", list);
}

void MainWindow::addHiddenApp(const QString& appName)
{
    QStringList hidden = getHiddenApps();
    if (!hidden.contains(appName)) {
        hidden.append(appName);
        setHiddenApps(hidden);
    }
}

void MainWindow::removeHiddenApp(const QString& appName)
{
    QStringList hidden = getHiddenApps();
    hidden.removeAll(appName);
    setHiddenApps(hidden);
}

bool CheckCoreProcess(const QString& processName) {
    static const QSet<QString> coreProcesses = {
        "System", "System Idle Process", "smss.exe", "csrss.exe",
        "wininit.exe", "services.exe", "lsass.exe", "winlogon.exe",
        "explorer.exe", "svchost.exe", "dwm.exe", "taskhostw.exe",
        "spoolsv.exe", "wuauclt.exe"
    };
    return coreProcesses.contains(processName);
}

bool CheckHiddenApp(const QString& processName) {
    QSettings settings("YouCompany", "AppTimeLimiter");
    const QList<QString> HiddenApps= settings.value("HiddenApps").toStringList();
    return HiddenApps.contains(processName);
}

void MainWindow::onAddToLimited()
{
    if (m_contextAppName.isEmpty()) return;

    // 检查是否已经是限时应用
    if (m_monitor->getLimitedApps().contains(m_contextAppName)) {
        QMessageBox::information(this, "提示", "该应用已在限时列表中。");
        return;
    }

    // 如果是系统进程，拒绝（调用 CheckCoreProcess）
    if (CheckCoreProcess(m_contextAppName)) {
        QMessageBox::warning(this, "提示", "系统进程不能限时。");
        return;
    }

    // 如果是本程序，幽默一下
    if (m_contextAppName == "AppTimeLimiter.exe") {
        QMessageBox::warning(this, "提示", "你想限制自己？😂");
        return;
    }

    // 让用户输入限制时长
    bool ok = false;
    int limitMinutes = QInputDialog::getInt(
        this, "设置限制时长",
        QString("为 \"%1\" 设置每日限制时长（分钟）:").arg(m_contextAppName),
        30, 1, 1440, 1, &ok
    );
    if (!ok) return;

    // 添加到限时列表
    m_monitor->addLimitedApp(m_contextAppName, limitMinutes);

    // 刷新表格
    refreshUsageTable();
    refreshLimitTable();

    QMessageBox::information(this, "成功",
        QString("已将 \"%1\" 添加到限时应用，每日限制 %2 分钟。")
        .arg(m_contextAppName).arg(limitMinutes));
}

// ==================== UI Initialization ====================
void MainWindow::showHiddenAppsManager()
{
    QStringList hiddenApps = getHiddenApps();
    if (hiddenApps.isEmpty()) {
        QMessageBox::information(this, "提示", "当前没有隐藏的应用。");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("管理隐藏应用");
    dialog.setMinimumSize(300, 200);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QListWidget* listWidget = new QListWidget(&dialog);
    for (const QString& app : hiddenApps) {
        // 显示友好名称，但存储进程名
        QString friendly = m_monitor->getFriendlyAppName(app);
        QListWidgetItem* item = new QListWidgetItem(friendly, listWidget);
        item->setData(Qt::UserRole, app);
    }
    layout->addWidget(listWidget);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* unhideBtn = new QPushButton("取消隐藏所选", &dialog);
    QPushButton* closeBtn = new QPushButton("关闭", &dialog);
    btnLayout->addWidget(unhideBtn);
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(unhideBtn, &QPushButton::clicked, [&]() {
        QListWidgetItem* current = listWidget->currentItem();
        if (!current) {
            QMessageBox::warning(&dialog, "提示", "请先选择一个应用。");
            return;
        }
        QString appName = current->data(Qt::UserRole).toString();
        if (!appName.isEmpty()) {
            removeHiddenApp(appName);
            refreshUsageTable();  // 刷新表格，立即显示
            QMessageBox::information(&dialog, "成功", QString("已取消隐藏 \"%1\"。").arg(appName));
            // 更新列表
            QStringList newHidden = getHiddenApps();
            listWidget->clear();
            for (const QString& app : newHidden) {
                QString friendly = m_monitor->getFriendlyAppName(app);
                QListWidgetItem* item = new QListWidgetItem(friendly, listWidget);
                item->setData(Qt::UserRole, app);
            }
            if (newHidden.isEmpty()) {
                QMessageBox::information(&dialog, "提示", "所有隐藏应用已恢复显示。");
                dialog.accept();
            }
        }
        });

    dialog.exec();
}

void MainWindow::onUsageTableCustomContextMenu(const QPoint& pos)
{
    QTableWidgetItem* item = m_usageTable->itemAt(pos);
    if (!item) return;   // 没有点击到有效项

    // 获取应用名称（存储在 UserRole 中，我们在刷新时保存了）
    QString appName = item->data(Qt::UserRole).toString();
    if (appName.isEmpty()) {
        // 如果 UserRole 为空，则从显示的文本中取（但要考虑可能有图标）
        appName = item->text();
    }
    if (appName.isEmpty()) return;

    // 记录当前选中的应用，供菜单操作使用
    m_contextAppName = appName;

    // 创建右键菜单
    QMenu menu(this);

    QAction* addAction = new QAction("添加到限时应用", this);
    QAction* addToGroupAction = new QAction("添加到应用组", this);
    QAction* hideAction = new QAction("隐藏此应用", this);

    connect(addAction, &QAction::triggered, this, &MainWindow::onAddToLimited);
    connect(addToGroupAction, &QAction::triggered, this, &MainWindow::onAddToGroup);
    connect(hideAction, &QAction::triggered, this, &MainWindow::onHideApp);

    menu.addAction(addAction);
    menu.addAction(addToGroupAction);
    menu.addAction(hideAction);

    // 如果已经有限时，可以禁用"添加"选项（可选）
    if (m_monitor->getLimitedApps().contains(m_contextAppName)) {
        addAction->setEnabled(false);
        addAction->setText("已在限时列表");
    }

    // 如果已在某个组中，禁用"添加到应用组"
    QString existingGroup = m_monitor->getAppGroup(m_contextAppName);
    if (!existingGroup.isEmpty()) {
        addToGroupAction->setEnabled(false);
        addToGroupAction->setText(QString("已在组: %1").arg(existingGroup));
    }

    // 如果已经隐藏，可以禁用“隐藏”选项（可选）
    QSettings settings("YourCompany", "AppTimeLimiter");
    QStringList hiddenList = settings.value("HiddenApps").toStringList();
    if (hiddenList.contains(m_contextAppName)) {
        hideAction->setEnabled(false);
        hideAction->setText("已隐藏");
    }

    // 显示菜单（相对于表格的全局位置）
    menu.exec(m_usageTable->viewport()->mapToGlobal(pos));
}

void MainWindow::initUI()
{
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(m_centralWidget);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    // --- Status section ---
    QGroupBox* statusGroup = new QGroupBox("今日使用概况");
    QHBoxLayout* statusLayout = new QHBoxLayout(statusGroup);

    m_totalTimeLabel = new QLabel("总使用时间: 0 小时 0 分钟");
    m_totalTimeLabel->setStyleSheet("font-size: 14px; font-weight: bold;");

    m_totalRemainingLabel = new QLabel("剩余时间: 无限制");
    m_totalRemainingLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: gray;");

    statusLayout->addWidget(m_totalTimeLabel);
    statusLayout->addStretch();
    statusLayout->addWidget(m_totalRemainingLabel);
    mainLayout->addWidget(statusGroup);

    // --- Usage statistics table ---
    QGroupBox* usageGroup = new QGroupBox("应用使用统计");
    QVBoxLayout* usageLayout = new QVBoxLayout(usageGroup);

    m_usageTable = new QTableWidget(0, 3);
    m_usageTable->setHorizontalHeaderLabels({"应用名称", "使用时间", "是否限时"});
    m_usageTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_usageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_usageTable->setAlternatingRowColors(true);
    m_usageTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_usageTable, &QTableWidget::customContextMenuRequested,
        this, &MainWindow::onUsageTableCustomContextMenu);

    usageLayout->addWidget(m_usageTable);

    QHBoxLayout* usageBtnLayout = new QHBoxLayout();
    m_refreshBtn = new QPushButton("刷新统计");
    m_settingsBtn = new QPushButton("设置");
    m_historyBtn = new QPushButton("历史记录");
    usageBtnLayout->addWidget(m_refreshBtn);
    usageBtnLayout->addWidget(m_historyBtn);
    usageBtnLayout->addStretch();
    usageBtnLayout->addWidget(m_settingsBtn);
    usageLayout->addLayout(usageBtnLayout);

    mainLayout->addWidget(usageGroup);

    // --- Limit management section (with collapsible group panel) ---
    QGroupBox* limitGroup = new QGroupBox("限时应用管理");
    
    QVBoxLayout* limitLayout = new QVBoxLayout(limitGroup);
    

    

    // 主内容区域：左侧限时应用表 + 右侧分组面板
    QHBoxLayout* contentLayout = new QHBoxLayout();

    // 限时应用表 + 按钮
    QWidget* leftPanel = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_limitTable = new QTableWidget(0, 4);
    m_limitTable->setHorizontalHeaderLabels({"应用名称", "限制时长(分钟)", "已使用(分钟)", "剩余(分钟)"});
    m_limitTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_limitTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_limitTable->setAlternatingRowColors(true);
    leftLayout->addWidget(m_limitTable);

    QHBoxLayout* limitBtnLayout = new QHBoxLayout();
    m_addLimitBtn = new QPushButton("添加限时应用");
    m_removeLimitBtn = new QPushButton("移除限时应用");
    limitBtnLayout->addWidget(m_addLimitBtn);
    limitBtnLayout->addWidget(m_removeLimitBtn);
    limitBtnLayout->addStretch();
    leftLayout->addLayout(limitBtnLayout);

    contentLayout->addWidget(leftPanel);
    limitLayout->addLayout(contentLayout);

    mainLayout->addWidget(limitGroup);

    //------------------------------------------------------------------------------------------------------------

    // -- Collapsible group panel ---
    QGroupBox* AppGroups = new QGroupBox("应用分组");
    QVBoxLayout* AppGroup = new QVBoxLayout(AppGroups);

    QHBoxLayout* contentLayout2 = new QHBoxLayout();

    m_toggleExpandBtn = new QPushButton("展开应用分组");
    AppGroup->addWidget(m_toggleExpandBtn);

    // 右侧：应用分组面板（默认隐藏）
    m_groupPanel = new QWidget();
    QVBoxLayout* groupLayout = new QVBoxLayout(m_groupPanel);
    groupLayout->setContentsMargins(0, 0, 0, 0);

    m_groupTable = new QTableWidget(0, 4);
    m_groupTable->setHorizontalHeaderLabels({ "组名", "限制时长(分钟)", "已使用(分钟)", "剩余(分钟)" });
    m_groupTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_groupTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_groupTable->setAlternatingRowColors(true);
    groupLayout->addWidget(m_groupTable);

    QHBoxLayout* groupBtnLayout = new QHBoxLayout();
    m_createGroupBtn = new QPushButton("创建组");
    m_deleteGroupBtn = new QPushButton("删除组");
    m_setGroupLimitBtn = new QPushButton("设置组限时");
    groupBtnLayout->addWidget(m_createGroupBtn);
    groupBtnLayout->addWidget(m_deleteGroupBtn);
    groupBtnLayout->addWidget(m_setGroupLimitBtn);
    groupLayout->addLayout(groupBtnLayout);

    QHBoxLayout* groupBtnLayout2 = new QHBoxLayout();
    m_addToGroupBtn = new QPushButton("添加应用到组");
    m_removeFromGroupBtn = new QPushButton("从组中移除");
    groupBtnLayout2->addWidget(m_addToGroupBtn);
    groupBtnLayout2->addWidget(m_removeFromGroupBtn);
    groupLayout->addLayout(groupBtnLayout2);

    m_groupPanel->setVisible(false);
    contentLayout2->addWidget(m_groupPanel, 1);

    AppGroup->addLayout(contentLayout2);

    //-------------------------------------------------------------------------------------------------------------

    /*
    // 右侧：应用分组面板（默认隐藏）
    m_groupPanel = new QWidget();
    QVBoxLayout* groupLayout = new QVBoxLayout(m_groupPanel);
    groupLayout->setContentsMargins(0, 0, 0, 0);

    m_groupTable = new QTableWidget(0, 4);
    m_groupTable->setHorizontalHeaderLabels({"组名", "限制时长(分钟)", "已使用(分钟)", "剩余(分钟)"});
    m_groupTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_groupTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_groupTable->setAlternatingRowColors(true);
    groupLayout->addWidget(m_groupTable);

    QHBoxLayout* groupBtnLayout = new QHBoxLayout();
    m_createGroupBtn = new QPushButton("创建组");
    m_deleteGroupBtn = new QPushButton("删除组");
    m_setGroupLimitBtn = new QPushButton("设置组限时");
    groupBtnLayout->addWidget(m_createGroupBtn);
    groupBtnLayout->addWidget(m_deleteGroupBtn);
    groupBtnLayout->addWidget(m_setGroupLimitBtn);
    groupLayout->addLayout(groupBtnLayout);

    QHBoxLayout* groupBtnLayout2 = new QHBoxLayout();
    m_addToGroupBtn = new QPushButton("添加应用到组");
    m_removeFromGroupBtn = new QPushButton("从组中移除");
    groupBtnLayout2->addWidget(m_addToGroupBtn);
    groupBtnLayout2->addWidget(m_removeFromGroupBtn);
    groupLayout->addLayout(groupBtnLayout2);

    m_groupPanel->setVisible(false);
    contentLayout->addWidget(m_groupPanel, 1);

    limitLayout->addLayout(contentLayout);
    */

    mainLayout->addWidget(AppGroups);

    // Connect buttons
    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    connect(m_historyBtn, &QPushButton::clicked, this, &MainWindow::onHistoryClicked);
    connect(m_addLimitBtn, &QPushButton::clicked, this, &MainWindow::onAddLimitClicked);


    connect(m_removeLimitBtn, &QPushButton::clicked, this, &MainWindow::onRemoveLimitClicked);  //**这是程序源代码,下面为改动
    /*
    connect(m_removeLimitBtn, &QPushButton::clicked, this, [=]() {
        if (verifyAdminPassword()) {
            MainWindow::onRemoveLimitClicked;
        }
        });*/

    // 分组按钮连接
    connect(m_toggleExpandBtn, &QPushButton::clicked, this, &MainWindow::onToggleExpand);
    connect(m_createGroupBtn, &QPushButton::clicked, this, &MainWindow::onCreateGroup);
    connect(m_deleteGroupBtn, &QPushButton::clicked, this, &MainWindow::onDeleteGroup);
    connect(m_setGroupLimitBtn, &QPushButton::clicked, this, &MainWindow::onSetGroupLimit);
    connect(m_addToGroupBtn, &QPushButton::clicked, this, &MainWindow::onAddToGroup);
    connect(m_removeFromGroupBtn, &QPushButton::clicked, this, &MainWindow::onRemoveFromGroup);
}

void MainWindow::initTray()
{
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIconNormal = QIcon(":/icon.png");
    m_trayIcon->setIcon(m_trayIconNormal);
    m_trayIcon->setToolTip("健康使用电脑");

    // Prepare the warning icon (generated at runtime, no extra resource needed)
    // and a blink timer used when a usage limit is reached.
    m_trayIconWarning = createWarningIcon();
    m_trayBlinkTimer = new QTimer(this);
    m_trayBlinkTimer->setInterval(500);
    connect(m_trayBlinkTimer, &QTimer::timeout, this, &MainWindow::onTrayBlinkTimeout);

    m_trayMenu = new QMenu(this);
    QAction* showAction = new QAction("显示主窗口", this);
    QAction* settingsAction = new QAction("设置", this);
    QAction* quitAction = new QAction("退出", this);

    connect(showAction, &QAction::triggered, [=]() {
        stopTrayBlink();
        show();
        raise();
        activateWindow();
    });

    connect(settingsAction, &QAction::triggered, this, &MainWindow::onSettingsClicked);

    connect(quitAction, &QAction::triggered, [=]() {
        if (verifyAdminPassword()) {
            m_monitor->stop();
            m_trayIcon->showMessage("健康使用电脑", "程序已退出，将无法监测使用情况");
            //QApplication::quit();
            QCoreApplication::exit(1);
        }
    });

    m_trayMenu->addAction(showAction);
    m_trayMenu->addAction(settingsAction);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(quitAction);

    m_trayIcon->setContextMenu(m_trayMenu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, [=](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            stopTrayBlink();
            show();
            raise();
            activateWindow();
        }
    });

    m_trayIcon->show();
    m_trayIcon->showMessage("健康使用电脑", "程序已在后台运行，双击托盘图标打开主窗口。");
}

// ==================== Tray blink / Toast notifications ====================

QIcon MainWindow::createWarningIcon()
{
    // Draw a red circle with a white "!" so the tray visibly changes on a limit.
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#E53935"));
    p.drawEllipse(4, 4, 56, 56);
    p.setPen(QPen(Qt::white, 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::white);
    p.drawLine(32, 20, 32, 40);
    p.drawEllipse(32, 48, 4, 4);
    return QIcon(pm);
}

void MainWindow::onTrayBlinkTimeout()
{
    m_trayBlinkState = !m_trayBlinkState;
    m_trayIcon->setIcon(m_trayBlinkState ? m_trayIconWarning : m_trayIconNormal);
}

void MainWindow::startTrayBlink()
{
    if (!m_trayBlinkTimer->isActive())
        m_trayBlinkTimer->start();
}

void MainWindow::stopTrayBlink()
{
    if (m_trayBlinkTimer->isActive())
        m_trayBlinkTimer->stop();
    m_trayBlinkState = false;
    m_trayIcon->setIcon(m_trayIconNormal);
}

QString MainWindow::formatDuration(int secs)
{
    if (secs < 0) secs = 0;
    int h = secs / 3600;
    int m = (secs % 3600) / 60;
    int s = secs % 60;
    if (h > 0) return QString("%1 小时 %2 分").arg(h).arg(m);
    if (m > 0) return QString("%1 分 %2 秒").arg(m).arg(s);
    return QString("%1 秒").arg(s);
}

void MainWindow::showToast(const QString& title, const QString& message)
{
    // 自定义 Toast：黑底白字，从屏幕顶部中央滑入，可堆叠，显示约 2.5 秒后滑出。
    ToastWidget* toast = new ToastWidget(title, message);
    toast->onClosed = [this](ToastWidget* w) {
        m_activeToasts.removeAll(w);
        w->deleteLater();
        repositionToasts();
    };
    m_activeToasts.append(toast);
    repositionToasts();
    QTimer::singleShot(2500, toast, [toast]() { toast->slideOut(); });
}

void MainWindow::repositionToasts()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    QRect sg = screen->availableGeometry();

    int y = 16;
    const int gap = 8;
    for (int i = 0; i < m_activeToasts.size(); ++i) {
        ToastWidget* t = m_activeToasts[i];
        int x = sg.center().x() - t->width() / 2;
        QPoint target(x, y);
        if (t->isVisible())
            t->moveToPos(target);
        else
            t->slideIn(target);
        y += t->height() + gap;
    }
}

void MainWindow::onAppOpened(const QString& processName)
{
    // 过滤系统进程与自身，避免无意义提示刷屏
    if (processName.isEmpty()) return;
    if (CheckCoreProcess(processName)) return;
    if (CheckHiddenApp(processName)) return;
    if (processName.compare("AppTimeLimiter.exe", Qt::CaseInsensitive) == 0) return;

    QString friendly = m_monitor->getFriendlyAppName(processName);
    int limitMins = m_monitor->getAppLimitMinutes(processName);

    if (limitMins > 0 && m_monitor->getLimitedApps().contains(processName)) {
        int usedSecs = m_monitor->getAppUsedSeconds(processName);
        int remainingSecs = limitMins * 60 - usedSecs;
        QString msg;
        if (remainingSecs <= 0) {
            msg = QString("「%1」今日可用时间已用完").arg(friendly);
        } else {
            msg = QString("「%1」每日限制 %2，剩余可用 %3")
                .arg(friendly).arg(formatDuration(limitMins * 60)).arg(formatDuration(remainingSecs));
        }
        showToast("应用已打开", msg);
    } else {
        int usedSecs = m_monitor->getAppUsedSeconds(processName);
        int totalSecs = m_monitor->getShownTotalUsedSeconds();
        QString msg = QString("「%1」已使用 %2\n今日电脑总计 %3")
            .arg(friendly).arg(formatDuration(usedSecs)).arg(formatDuration(totalSecs));
        showToast("应用已打开", msg);
    }
}

// ==================== UI Refresh ====================

void MainWindow::refreshUsageTable()
{
    QMap<QString, int> usage = m_monitor->getAllUsage();
    QStringList limitedApps = m_monitor->getLimitedApps();
    QStringList hiddenApps = getHiddenApps();

    for (const QString& app : hiddenApps) {
        usage.remove(app);
    }

    // 按使用时间降序排列（代码不变）
    QList<QPair<QString, int>> sortedUsage;
    for (auto it = usage.begin(); it != usage.end(); ++it) {
        sortedUsage.append(qMakePair(it.key(), it.value()));
    }
    std::sort(sortedUsage.begin(), sortedUsage.end(),
        [](const QPair<QString, int>& a, const QPair<QString, int>& b) {
            return a.second > b.second;
        });

    m_usageTable->setRowCount(sortedUsage.size());

    for (int i = 0; i < sortedUsage.size(); ++i) {
        QString processName = sortedUsage[i].first;
        int secs = sortedUsage[i].second;

        // ---------- 获取友好名称和图标 ----------
        QString friendlyName = m_monitor->getFriendlyAppName(processName);
        QIcon icon = m_monitor->getAppIcon(processName);

        // 如果是本程序，特殊标记（保持你之前的幽默感）
        if (processName == "AppTimeLimiter.exe") {
            friendlyName = friendlyName + " (此应用)";
        }

        // 创建带图标的列表项
        QTableWidgetItem* nameItem = new QTableWidgetItem(icon, friendlyName);
        nameItem->setData(Qt::UserRole, processName);  // 保存原始进程名，便于后续操作

        // 格式化时间
        int hours = secs / 3600;
        int mins = (secs % 3600) / 60;
        int sec = secs % 60;
        QString timeStr = QString("%1时%2分%3秒").arg(hours).arg(mins).arg(sec);

        // 是否限时
        QString limited = limitedApps.contains(processName) ? "是" : "否";

        m_usageTable->setItem(i, 0, nameItem);
        m_usageTable->setItem(i, 1, new QTableWidgetItem(timeStr));
        m_usageTable->setItem(i, 2, new QTableWidgetItem(limited));
    }
}

void MainWindow::refreshLimitTable()
{
    QStringList limitedApps = m_monitor->getLimitedApps();
    m_limitTable->setRowCount(limitedApps.size());

    for (int i = 0; i < limitedApps.size(); ++i) {
        const QString& processName = limitedApps[i];

        // ---------- 获取友好名称和图标 ----------
        QString friendlyName = m_monitor->getFriendlyAppName(processName);
        QIcon icon = m_monitor->getAppIcon(processName);

        QTableWidgetItem* nameItem = new QTableWidgetItem(icon, friendlyName);
        nameItem->setData(Qt::UserRole, processName);

        int limitMins = m_monitor->getAppLimitMinutes(processName);
        int usedSecs = m_monitor->getAppUsedSeconds(processName);
        int usedMins = usedSecs / 60;
        int remainingMins = limitMins - usedMins;
        if (remainingMins < 0) remainingMins = 0;

        m_limitTable->setItem(i, 0, nameItem);
        m_limitTable->setItem(i, 1, new QTableWidgetItem(QString::number(limitMins)));
        m_limitTable->setItem(i, 2, new QTableWidgetItem(QString::number(usedMins)));
        m_limitTable->setItem(i, 3, new QTableWidgetItem(QString::number(remainingMins)));
    }
}

void MainWindow::updateStatusLabels()
{
    int totalSecs = m_monitor->getShownTotalUsedSeconds();
    int hours = totalSecs / 3600;
    int mins = (totalSecs % 3600) / 60;

    m_totalTimeLabel->setText(QString("总使用时间: %1 小时 %2 分钟").arg(hours).arg(mins));

    int totalLimit = m_monitor->getTotalLimitMinutes();
    if (totalLimit > 0) {
        int usedMins = totalSecs / 60;
        int remaining = totalLimit - usedMins;
        if (remaining < 0) remaining = 0;
        m_totalRemainingLabel->setText(QString("剩余时间: %1 分钟").arg(remaining));

        if (remaining <= 10) {
            m_totalRemainingLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: red;");
        } else if (remaining <= 30) {
            m_totalRemainingLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: orange;");
        } else {
            m_totalRemainingLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: green;");
        }
    } else {
        m_totalRemainingLabel->setText("剩余时间: 无限制");
        m_totalRemainingLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: gray;");
    }
}

// ==================== Signal Handlers ====================
void MainWindow::onHideApp()
{
    if (m_contextAppName.isEmpty()) return;

    // 询问确认
    int ret = QMessageBox::question(this, "确认隐藏",
        QString("确定要将 \"%1\" 从统计列表中隐藏吗？隐藏后不再计入总时间\n（可以在设置中取消隐藏）")
        .arg(m_contextAppName),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    if (!verifyAdminPassword()) return;
    // 添加到隐藏列表
    addHiddenApp(m_contextAppName);

    // 刷新表格（立即隐藏）
    refreshUsageTable();

    QMessageBox::information(this, "已隐藏",
        QString("\"%1\" 已隐藏，不再显示在统计列表中。")
        .arg(m_contextAppName));
}

void MainWindow::onAppUsageUpdated(const QString& processName, int usedSeconds)
{
    Q_UNUSED(processName)
    Q_UNUSED(usedSeconds)

    // Refresh tables every 10 updates to avoid excessive repaints
    static int counter = 0;
    counter++;
    if (counter % 10 == 0) {
        refreshUsageTable();
        refreshLimitTable();
        updateStatusLabels();
    }
}

void MainWindow::onTotalUsageUpdated(int usedSeconds)
{
    Q_UNUSED(usedSeconds)

    static int counter = 0;
    counter++;
    if (counter % 10 == 0) {
        updateStatusLabels();
    }
}

void MainWindow::onAppLimitReached(const QString& processName)
{
    qDebug() << "[MainWindow] App limit reached:" << processName;

    // Kill the app's process instead of locking the screen
    m_monitor->killProcess(processName);

    // Show the AppLimitDialog instead of lock screen
    if (m_appLimitDialog) {
        m_appLimitDialog->close(); // Close any previous dialog first
    }

    m_appLimitDialog = new AppLimitDialog(processName, this);
    connect(m_appLimitDialog, &AppLimitDialog::extendRequested,
            this, &MainWindow::onAppLimitExtendRequested);

    // When dialog is closed (either by "知道了" or normal close), clean up
    connect(m_appLimitDialog, &QDialog::finished, [=]() {
        if (m_appLimitDialog) {
            //m_appLimitDialog->deleteLater();
            m_appLimitDialog = nullptr;
        }
        // Refresh UI after dialog closes
        refreshUsageTable();
        refreshLimitTable();
        updateStatusLabels();
        stopTrayBlink();
    });

    m_appLimitDialog->show();
    m_appLimitDialog->raise();
    m_appLimitDialog->activateWindow();

    QString friendly = m_monitor->getFriendlyAppName(processName);
    startTrayBlink();
    showToast("应用已达限时", QString("「%1」的使用时间已用完").arg(friendly));
}

void MainWindow::onTotalLimitReached()
{
    if (m_isLocked) return;

    qDebug() << "[MainWindow] Total usage limit reached";
    m_isLocked = true;
    m_monitor->setMonitoringPaused(true);

    m_lockScreen->lockScreen("今日电脑使用总时长已达限制");

    startTrayBlink();
    showToast("总时长已达上限", "今日电脑使用总时长已达限制");
}

void MainWindow::onLockScreenUnlocked()
{
    qDebug() << "[MainWindow] Lock screen unlocked";
    m_isLocked = false;
    m_monitor->onLockScreenUnlocked();
    stopTrayBlink();

    // Refresh UI
    refreshUsageTable();
    refreshLimitTable();
    updateStatusLabels();
}

void MainWindow::onAppLimitExtendRequested(const QString& processName)
{
    qDebug() << "[MainWindow] Extending time for" << processName;

    // Clear the override flag so the limit can re-trigger again when time runs out
    m_monitor->clearAppLimitOverride(processName);

    // Reset the app's usage counter so the user gets a fresh full period
    m_monitor->resetAppUsage(processName);

    // Refresh UI
    refreshUsageTable();
    refreshLimitTable();
    updateStatusLabels();

    QMessageBox::information(nullptr, "延长成功",
        QString("已延长应用 \"%1\" 的使用时间。\n使用时间计数已重置，限时将在下次达到限制时再次生效。")
            .arg(processName));
}

// ==================== Button Handlers ====================

void MainWindow::onSettingsClicked()
{
    if (!verifyAdminPassword()) return;

    SettingsDialog dialog(this);
    
    connect(&dialog, &SettingsDialog::showHiddenAppsRequested, this, &MainWindow::showHiddenAppsManager);
    connect(&dialog, &SettingsDialog::clearHistoryRequested, this, [=]() {
        m_monitor->clearHistoryExceptToday();
    });
    connect(&dialog, &SettingsDialog::autoCleanDateApplied, this, [=](const QDate& cutoff) {
        m_monitor->clearHistoryBeforeDate(cutoff);
    });
    if (dialog.exec() == QDialog::Accepted) {
        refreshLimitTable();
        updateStatusLabels();
    }
}

void MainWindow::onAddLimitClicked()
{
    // Let user input process name
    bool ok = false;
    QString processName = QInputDialog::getText(
        this, "添加限时应用",
        "请输入应用进程名 (如 chrome.exe):",
        QLineEdit::Normal, "", &ok
    );

    if (!ok || processName.trimmed().isEmpty()) return;

    if (CheckCoreProcess(processName)) {
        QMessageBox::warning(this, "提示", QString("应用 \"%1\" 为系统进程，不可限时!").arg(processName));
        return;
    }

    if (processName == "AppTimeLimiter.exe" || processName == "apptimelimiter.exe") {
        QMessageBox::warning(this, "提示", QString("你是想让我把自己结束吗?"));
        return;
    }

    processName = processName.trimmed();

    // Check if already limited
    if (m_monitor->getLimitedApps().contains(processName)) {
        QMessageBox::warning(this, "提示", QString("应用 \"%1\" 已在限时列表中!").arg(processName));
        return;
    }

    // Set time limit
    int limitMinutes = QInputDialog::getInt(
        this, "设置限制时长",
        QString("请输入 \"%1\" 的每日限制时长:").arg(processName),
        30, 1, 1440, 1, &ok
    );

    if (!ok) return;

    m_monitor->addLimitedApp(processName, limitMinutes);
    refreshLimitTable();
    refreshUsageTable();
    QMessageBox::information(this, "成功",
        QString("已添加限时应用: %1\n每日限制时长: %2 分钟").arg(processName).arg(limitMinutes));
}

void MainWindow::onRemoveLimitClicked()
{
    int row = m_limitTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "提示", "请先在列表中选择要移除的限时应用");
        return;
    }

    if (!verifyAdminPassword()) {
        return;
    }

    // 【关键修改】从 UserRole 获取进程名
    QTableWidgetItem* item = m_limitTable->item(row, 0);
    if (!item) return;
    QString processName = item->data(Qt::UserRole).toString();
    if (processName.isEmpty()) {
        // 兼容旧版本：如果没有 UserRole，则尝试从文本提取（但新版本一定设置）
        processName = item->text();
    }

    int ret = QMessageBox::question(this, "确认",
        QString("确定要移除 \"%1\" 的限时设置吗?").arg(processName),
        QMessageBox::Yes | QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    m_monitor->removeLimitedApp(processName);
    refreshLimitTable();
    refreshUsageTable();
    QMessageBox::information(this, "成功", QString("已移除限时应用: %1").arg(processName));
}

void MainWindow::onRefreshClicked()
{
    refreshUsageTable();
    refreshLimitTable();
    refreshGroupTable();
    updateStatusLabels();
}

void MainWindow::onHistoryClicked()
{
    HistoryDialog dialog(m_monitor, this);
    dialog.exec();
}

// ==================== 应用分组 ====================

void MainWindow::refreshGroupTable()
{
    QStringList groups = m_monitor->getGroups();
    m_groupTable->setRowCount(groups.size());

    for (int i = 0; i < groups.size(); ++i) {
        const QString& groupName = groups[i];

        QTableWidgetItem* nameItem = new QTableWidgetItem(groupName);
        nameItem->setData(Qt::UserRole, groupName);

        int limitMins = m_monitor->getGroupLimitMinutes(groupName);
        int usedSecs = m_monitor->getGroupUsedSeconds(groupName);
        int usedMins = usedSecs / 60;
        int remainingMins = (limitMins > 0) ? (limitMins - usedMins) : 0;
        if (remainingMins < 0) remainingMins = 0;

        m_groupTable->setItem(i, 0, nameItem);
        m_groupTable->setItem(i, 1, new QTableWidgetItem(limitMins > 0 ? QString::number(limitMins) : "无限制"));
        m_groupTable->setItem(i, 2, new QTableWidgetItem(QString::number(usedMins)));
        m_groupTable->setItem(i, 3, new QTableWidgetItem(limitMins > 0 ? QString::number(remainingMins) : "无限制"));
    }
}

void MainWindow::onToggleExpand()
{
    m_expanded = !m_expanded;
    m_toggleExpandBtn->setText(m_expanded ? "折叠应用分组" : "展开应用分组");

    // Animate the panel height (smooth expand / collapse) instead of an instant
    // show/hide. We animate maximumHeight: the layout respects it, so the panel
    // grows/shrinks naturally. Keep it visible during the collapse animation.
    if (m_expanded)
        m_groupPanel->setVisible(true);

    if (!m_expandAnim) {
        m_expandAnim = new QPropertyAnimation(m_groupPanel, "maximumHeight", this);
        m_expandAnim->setDuration(250);
        m_expandAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_expandAnim, &QPropertyAnimation::finished, this, [this]() {
            if (!m_expanded)
                m_groupPanel->setVisible(false);
            m_groupPanel->setMaximumHeight(QWIDGETSIZE_MAX);
        });
    }
    m_expandAnim->stop();
    m_expandAnim->setStartValue(m_expanded ? 0 : m_groupPanel->height());
    int target = m_expanded ? qMax(m_groupPanel->sizeHint().height(), 100) : 0;
    m_expandAnim->setEndValue(target);
    m_expandAnim->start();
}

void MainWindow::onCreateGroup()
{
    if (!verifyAdminPassword()) {
        return;
    }

    bool ok = false;
    QString groupName = QInputDialog::getText(
        this, "创建应用组",
        "请输入组名:", QLineEdit::Normal, "", &ok
    );

    if (!ok || groupName.trimmed().isEmpty()) return;
    groupName = groupName.trimmed();

    QStringList groups = m_monitor->getGroups();
    if (groups.contains(groupName)) {
        QMessageBox::warning(this, "提示", "该组名已存在!");
        return;
    }

    m_monitor->addGroup(groupName);
    refreshGroupTable();
    QMessageBox::information(this, "成功", QString("已创建应用组: %1").arg(groupName));
}

void MainWindow::onDeleteGroup()
{
    int row = m_groupTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "提示", "请先在列表中选择要删除的组");
        return;
    }


    QString groupName = m_groupTable->item(row, 0)->data(Qt::UserRole).toString();

    int ret = QMessageBox::question(this, "确认",
        QString("确定要删除组 \"%1\" 吗?\n组内的应用将不再受组限时限制。").arg(groupName),
        QMessageBox::Yes | QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    if (!verifyAdminPassword()) {
        return;
    }
    else {
        m_monitor->removeGroup(groupName);
        refreshGroupTable();
        QMessageBox::information(this, "成功", QString("已删除组: %1").arg(groupName));

    }
}

void MainWindow::onSetGroupLimit()
{
    int row = m_groupTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "提示", "请先在列表中选择要设置限时的组");
        return;
    }

    if (!verifyAdminPassword()) return;

    QString groupName = m_groupTable->item(row, 0)->data(Qt::UserRole).toString();

    bool ok = false;
    int limitMinutes = QInputDialog::getInt(
        this, "设置组限时",
        QString("为组 \"%1\" 设置每日总限时(分钟，0=无限制):").arg(groupName),
        60, 0, 1440, 1, &ok
    );

    if (!ok) return;

    m_monitor->setGroupLimit(groupName, limitMinutes);
    refreshGroupTable();
    QMessageBox::information(this, "成功",
        QString("已设置组 \"%1\" 的每日限时为 %2 分钟").arg(groupName).arg(limitMinutes));
}

void MainWindow::onAddToGroup()
{
    // 如果是从右键菜单触发的，m_contextAppName 已设置
    QString processName = m_contextAppName;

    if (processName.isEmpty()) {
        // 从分组面板按钮触发，让用户输入
        bool ok = false;
        processName = QInputDialog::getText(
            this, "添加应用到组",
            "请输入应用进程名(如 chrome.exe):", QLineEdit::Normal, "", &ok
        );
        if (!ok || processName.trimmed().isEmpty()) return;
        processName = processName.trimmed();
    }

    if (!verifyAdminPassword()) return;

    // 检查是否已在某个组中
    QString existingGroup = m_monitor->getAppGroup(processName);
    if (!existingGroup.isEmpty()) {
        QMessageBox::information(this, "提示", "一个应用只能添加到一个组");
        return;
    }

    // 检查是否有组存在
    QStringList groups = m_monitor->getGroups();
    if (groups.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先创建一个组");
        return;
    }

    // 选择目标组
    bool ok = false;
    QString groupName = QInputDialog::getItem(
        this, "选择组",
        QString("将 \"%1\" 添加到哪个组?").arg(processName),
        groups, 0, false, &ok
    );

    if (!ok || groupName.isEmpty()) return;

    m_monitor->addAppToGroup(processName, groupName);
    refreshGroupTable();
    refreshUsageTable();
    QMessageBox::information(this, "成功",
        QString("已将 \"%1\" 添加到组 \"%2\"").arg(processName).arg(groupName));
}

void MainWindow::onRemoveFromGroup()
{
    int row = m_groupTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "提示", "请先在列表中选择一个组");
        return;
    }

    if (!verifyAdminPassword()) return;

    QString groupName = m_groupTable->item(row, 0)->data(Qt::UserRole).toString();
    QStringList apps = m_monitor->getGroupApps(groupName);

    if (apps.isEmpty()) {
        QMessageBox::information(this, "提示", QString("组 \"%1\" 中没有应用").arg(groupName));
        return;
    }

    // 显示组内应用列表供选择
    QStringList friendlyApps;
    for (const QString& app : apps) {
        friendlyApps << m_monitor->getFriendlyAppName(app);
    }

    bool ok = false;
    QString selected = QInputDialog::getItem(
        this, "从组中移除应用",
        QString("从组 \"%1\" 中移除哪个应用?").arg(groupName),
        friendlyApps, 0, false, &ok
    );

    if (!ok || selected.isEmpty()) return;

    int idx = friendlyApps.indexOf(selected);
    if (idx < 0 || idx >= apps.size()) return;

    QString processName = apps[idx];
    m_monitor->removeAppFromGroup(processName);
    refreshGroupTable();
    refreshUsageTable();
    QMessageBox::information(this, "成功",
        QString("已将 \"%1\" 从组 \"%2\" 中移除").arg(processName).arg(groupName));
}

void MainWindow::onGroupLimitReached(const QString& groupName, const QString& processName)
{
    qDebug() << "[MainWindow] Group limit reached:" << groupName << "app:" << processName;

    // 杀掉当前前台进程
    m_monitor->killProcess(processName);

    // 显示组限时弹窗
    if (m_groupLimitDialog) {
        m_groupLimitDialog->close();
    }

    m_groupLimitDialog = new GroupLimitDialog(groupName, processName, this);
    connect(m_groupLimitDialog, &GroupLimitDialog::extendRequested,
            this, &MainWindow::onGroupLimitExtendRequested);

    connect(m_groupLimitDialog, &QDialog::finished, [=]() {
        if (m_groupLimitDialog) {
            m_groupLimitDialog = nullptr;
        }
        refreshUsageTable();
        refreshLimitTable();
        refreshGroupTable();
        updateStatusLabels();
        stopTrayBlink();
    });

    m_groupLimitDialog->show();
    m_groupLimitDialog->raise();
    m_groupLimitDialog->activateWindow();

    startTrayBlink();
    showToast("应用组已达限时", QString("应用组「%1」的使用时间已用完").arg(groupName));
}

void MainWindow::onGroupLimitExtendRequested(const QString& groupName)
{
    qDebug() << "[MainWindow] Extending group limit for" << groupName;

    m_monitor->clearGroupLimitOverride(groupName);
    m_monitor->extendGroupLimit(groupName);

    refreshUsageTable();
    refreshLimitTable();
    refreshGroupTable();
    updateStatusLabels();

    QMessageBox::information(nullptr, "延长成功",
        QString("已延长组 \"%1\" 的使用时间。\n组使用时间计数已重置，限时将在下次达到限制时再次生效。")
            .arg(groupName));
}

// ==================== Utility ====================

bool MainWindow::verifyAdminPassword()
{
    bool ok = false;
    QString inputPassword = QInputDialog::getText(
        this, "身份验证",
        "请输入管理员密码以继续操作:",
        QLineEdit::Password, "", &ok
    );

    if (!ok) {
        QMessageBox::information(this, "健康使用电脑", "操作已取消", QMessageBox::Yes);
        return false;
    }

    QSettings settings("YourCompany", "AppTimeLimiter");
    QString correctPassword = settings.value("Password", "2026888").toString();


    if (inputPassword.trimmed() == correctPassword) {
        return true;
    }
    else {
        QMessageBox::information(this, "健康使用电脑", "密码错误!", QMessageBox::Yes);
        return false;
    }
}
/*
void MainWindow::closeEvent(QCloseEvent* event)
{
    // Minimize to tray instead of closing
    hide();
    event->ignore();
    m_trayIcon->showMessage("应用使用时间管理", "程序已最小化到系统托盘，后台持续监控中。");
}
*/
void MainWindow::closeEvent(QCloseEvent* event) {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "确认", "是否退出程序？\n点击“否”会最小化到系统托盘",
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
        QMessageBox::Cancel
    );
    if (reply == QMessageBox::Yes&&verifyAdminPassword()) {
        // 退出
        m_monitor->stop();
        //QApplication::quit();
        QCoreApplication::exit(1);
    }
    else if (reply == QMessageBox::No) {
        // 最小化到托盘
        hide();
        event->ignore();
        m_trayIcon->showMessage("健康使用电脑", "程序已最小化到系统托盘，后台持续监控中。");
    }
    else {
        event->ignore();  // 取消操作
    }
}