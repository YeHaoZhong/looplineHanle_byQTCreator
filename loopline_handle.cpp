#include "loopline_handle.h"
#include "ui_loopline_handle.h"
#include <QTableView>
#include <QStatusBar>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QLabel>
#include <QPixmap>
#include <QFileInfo>
// #include <limits>      // std::numeric_limits
// #include <algorithm>   // std::min / std::max
#include <QDir>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QRegularExpression>
#include <QGraphicsDropShadowEffect>
#include <QCloseEvent>
#include <QGroupBox>
#include "sqlconnectionpool.h"
class SlotWidget : public QFrame {
    Q_OBJECT
public:
    explicit SlotWidget(int id, QWidget* parent = nullptr)
        : QFrame(parent), _id(id)
    {
        setFrameShape(QFrame::StyledPanel);
        setLineWidth(1);
        setMinimumSize(90, 60);
        QLabel* l = new QLabel(QString::number(id), this);
        l->setAlignment(Qt::AlignCenter);
        QVBoxLayout* L = new QVBoxLayout(this);
        L->setContentsMargins(4, 4, 4, 4);
        L->addWidget(l);
        setStatus("idle");
    }

    void setStatus(const QString& s) {
        if (s == "idle") setStyleSheet("background:#f3f6f9;border-radius:6px;");
        else if (s == "busy") setStyleSheet("background:#fff2cc;border-radius:6px;");
        else if (s == "error") setStyleSheet("background:#ffd1d1;border-radius:6px;");
    }
private:
    int _id;
};
loopline_handle::loopline_handle(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::loopline_handle)
{
    ui->setupUi(this);
    setWindowTitle(tr("巨鼎智能"));
    initUI();
    QTimer::singleShot(0, this, [this]() {
        startAsyncInit();		// 异步初始化, 后台连接设备等
    });
}
void loopline_handle::startAsyncInit()
{
    QThread* initThread = new QThread(this);
    InitWorker* worker = new InitWorker(&_dataProcessMain);

    worker->moveToThread(initThread);

    connect(initThread, &QThread::started,
            worker, &InitWorker::doInit);

    connect(worker, &InitWorker::initFinished,
            this, [this, initThread, worker](bool ok, const QString& err) {

                if (ok) {
                    log("---- [Init] All connections initialized over!");
                }
                else {
                    log("---- [Init] Failed: " + err.toStdString());
                }

                initThread->quit();
            });

    connect(initThread, &QThread::finished,
            worker, &QObject::deleteLater);
    connect(initThread, &QThread::finished,
            initThread, &QObject::deleteLater);

    initThread->start();
}
loopline_handle::~loopline_handle()
{
    delete ui;
}
void loopline_handle::initUI(){
    _mainPage = createMainPage();
    _queryPage = createQueryPage();

    _stack = new QStackedWidget(this);
    _stack->addWidget(_mainPage);
    _stack->addWidget(_queryPage);
    setCentralWidget(_stack);
    _stack->setCurrentIndex(0);

    _navList = new QListWidget(this);
    _navList->setSelectionMode(QAbstractItemView::SingleSelection);
    _navList->setFixedWidth(200);
    _navList->setSpacing(6);
    _navList->setFrameShape(QFrame::NoFrame);
    _navList->setFocusPolicy(Qt::NoFocus); // 如果你希望点击显示焦点不抢主控件，可去掉

    // 添加菜单项（顺序要与 _stack 页面一致）
    QListWidgetItem* it0 = new QListWidgetItem(tr("主页"), _navList);
    // it0->setIcon(QIcon(":/icons/home.png")); // 如有资源图标可启用
    QListWidgetItem* it1 = new QListWidgetItem(tr("单号查询"), _navList);
    // it1->setIcon(QIcon(":/icons/search.png"));

    _navList->addItem(it0);
    _navList->addItem(it1);

    // 选中默认项
    _navList->setCurrentRow(0);

    // 点击切换页面（使用 currentRowChanged 更直观）
    connect(_navList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        if (row < _stack->count()) {
            _stack->setCurrentIndex(row);
            // 如果切到查询页，聚焦输入框
            if (_stack->currentWidget() == _queryPage) {
                QMetaObject::invokeMethod(_queryEdit, "setFocus", Qt::QueuedConnection);
                _queryEdit->clear();
                clearResults();
            }
        }
    });

    _mainSplitter = new QSplitter(Qt::Horizontal, this);
    _mainSplitter->addWidget(_navList);
    _mainSplitter->addWidget(_stack);
    _mainSplitter->setStretchFactor(0, 0);
    _mainSplitter->setStretchFactor(1, 1);
    _mainSplitter->setCollapsible(0, false);
    _mainSplitter->setHandleWidth(6);

    // 将 splitter 设为 central widget
    setCentralWidget(_mainSplitter);
    this->setFixedSize(1500, 900); // 固定为 1024x768
}
QWidget* loopline_handle::createMainPage(){
    QWidget* container = new QWidget;
    // 主垂直布局，按区域分组
    QVBoxLayout* mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(12);

    mainLayout->addWidget(createLockGroup());
    mainLayout->addWidget(createCarTestGroup());
    mainLayout->addWidget(createOpsGroup());

    // ===== 4. 可选：状态网格/占位（若以后要放设备状态） =====
    // 如果你要放设备状态网格，考虑再做一个 QGroupBox，内部用 QGridLayout 动态填充 48 个小部件（QLabel/QFrame）
    // QGroupBox* statusGroup = new QGroupBox("设备状态");
    // ... (这里留空以便扩展)
    // mainLayout->addWidget(statusGroup);

    // ===== 初始按钮使能状态 =====
    if (_isDrivingCarTest.load()) {
        _runCarTestBtn->setEnabled(false);
        _stopCarTestBtn->setEnabled(true);
    }
    else {
        _runCarTestBtn->setEnabled(true);
        _stopCarTestBtn->setEnabled(false);
    }

    // ===== 使 container 的高度根据内容自适应，但允许伸缩 =====
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    return container;
}
QGroupBox* loopline_handle::createLockGroup()
{
    QGroupBox* lockGroup = new QGroupBox("小车锁定/解锁");
    QHBoxLayout* lockLayout = new QHBoxLayout(lockGroup);
    lockLayout->setContentsMargins(8, 6, 8, 6);

    _lockCarEdit = new QLineEdit;
    _lockCarEdit->setPlaceholderText("锁/解锁小车号");
    _lockCarEdit->setMinimumWidth(140);

    _lockCarBtn = new QPushButton("锁定小车");
    _unlockCarBtn = new QPushButton("解锁小车");
    _lockCarBtn->setMinimumWidth(100);
    _unlockCarBtn->setMinimumWidth(100);

    lockLayout->addStretch();
    lockLayout->addWidget(_lockCarEdit);
    lockLayout->addSpacing(6);
    lockLayout->addWidget(_lockCarBtn);
    lockLayout->addWidget(_unlockCarBtn);
    lockLayout->addStretch();

    // 信号连接（局部：保持创建后立即连接）
    connect(_lockCarBtn, &QPushButton::clicked, this, &loopline_handle::onLockCarButtonClicked);
    connect(_unlockCarBtn, &QPushButton::clicked, this, &loopline_handle::onUnlockCarButtonClicked);

    return lockGroup;
}
QGroupBox* loopline_handle::createCarTestGroup()
{
    QGroupBox* driveGroup = new QGroupBox("单车控制");
    QHBoxLayout* driveLayout = new QHBoxLayout(driveGroup);
    driveLayout->setContentsMargins(8, 6, 8, 6);

    _driveCarEdit = new QLineEdit;
    _driveCarEdit->setPlaceholderText("输入小车号");
    _driveCarEdit->setMinimumWidth(140);

    _driveCarBtn = new QPushButton("测试小车");
    _driveCarBtn->setMinimumWidth(120);

    driveLayout->addStretch();
    driveLayout->addWidget(_driveCarEdit);
    driveLayout->addSpacing(6);
    driveLayout->addWidget(_driveCarBtn);
    driveLayout->addStretch();

    connect(_driveCarBtn, &QPushButton::clicked, this, &loopline_handle::onDriveCarButtonClicked);
    connect(_driveCarEdit, &QLineEdit::returnPressed, this, &loopline_handle::onDriveCarButtonClicked);

    return driveGroup;
}
QGroupBox* loopline_handle::createOpsGroup()
{
    QGroupBox* opsGroup = new QGroupBox("设备操作");
    QGridLayout* opsGrid = new QGridLayout(opsGroup);
    opsGrid->setHorizontalSpacing(12);
    opsGrid->setVerticalSpacing(8);
    opsGrid->setContentsMargins(8, 6, 8, 6);

    _runCarTestBtn = new QPushButton("小车循环测试");
    _stopCarTestBtn = new QPushButton("停止测试");
    _resetCameraBtn = new QPushButton("重置相机位置");
    _resetSlotConfigBtn = new QPushButton("重置格口信息");
    _testCameraBtn = new QPushButton("相机触发测试");
    _resetDriverBtn = new QPushButton("重置驱动命令");

    QList<QPushButton*> btns = {
        _runCarTestBtn, _stopCarTestBtn,
        _resetCameraBtn, _resetSlotConfigBtn,
        _testCameraBtn, _resetDriverBtn
    };
    for (auto b : btns) { /*b->setMinimumHeight(34); b->setMinimumWidth(100);*/ b->setFixedSize(300, 34); }

    opsGrid->addWidget(_runCarTestBtn, 0, 0);
    opsGrid->addWidget(_stopCarTestBtn, 0, 1);
    opsGrid->addWidget(_resetCameraBtn, 1, 0);
    opsGrid->addWidget(_resetSlotConfigBtn, 1, 1);
    opsGrid->addWidget(_testCameraBtn, 2, 0);
    opsGrid->addWidget(_resetDriverBtn, 2, 1);

    connect(_runCarTestBtn, &QPushButton::clicked, this, &loopline_handle::onRunCarTestButtonClicked);
    connect(_stopCarTestBtn, &QPushButton::clicked, this, &loopline_handle::onStopCarTestButtonClicked);
    connect(_resetCameraBtn, &QPushButton::clicked, &_dataProcessMain, &DataProcessMain::resetCamerasPosition);
    connect(_resetSlotConfigBtn, &QPushButton::clicked, &_dataProcessMain, &DataProcessMain::resetSlotConfigurations);
    connect(_testCameraBtn, &QPushButton::clicked, &_dataProcessMain, &DataProcessMain::testCamera);
    connect(_resetDriverBtn, &QPushButton::clicked, &_dataProcessMain, &DataProcessMain::resetDriver);

    return opsGroup;
}
void loopline_handle::onRunCarTestButtonClicked()
{
    _dataProcessMain.startTestCarLoop();
    _isDrivingCarTest.store(true);
    _runCarTestBtn->setEnabled(false);
    _stopCarTestBtn->setEnabled(true);
}
void loopline_handle::onStopCarTestButtonClicked()
{
    _dataProcessMain.stopTestCarLoop();
    _isDrivingCarTest.store(false);
    _runCarTestBtn->setEnabled(true);
    _stopCarTestBtn->setEnabled(false);
}
void loopline_handle::onDriveCarButtonClicked()
{
    QString input = _driveCarEdit->text().trimmed();
    if (input.isEmpty()) {
        statusBar()->showMessage("请输入小车号!");
        return;
    }
    bool ok = false;
    int car_id = input.toInt(&ok);
    if (!ok || car_id <= 0) {
        statusBar()->showMessage("无效小车号!");
        return;
    }
    _driveCarBtn->setEnabled(false);
    // 启动小车（假设 DataProcessMain 有相应方法）
    _dataProcessMain.driveByCarid(car_id);

    _driveCarBtn->setEnabled(true);
}
void loopline_handle::onLockCarButtonClicked()
{
    QString input = _lockCarEdit->text().trimmed();
    if (input.isEmpty()) {
        statusBar()->showMessage("请输入小车号!");
        return;
    }
    bool ok = false;
    int car_id = input.toInt(&ok);
    if (!ok || car_id <= 0 || car_id>202)
    {
        statusBar()->showMessage("无效小车号!");
        return;
    }
    _lockCarBtn->setEnabled(false);
    _unlockCarBtn->setEnabled(false);
    _dataProcessMain.lockCar(car_id);
    _lockCarBtn->setEnabled(true);
    _unlockCarBtn->setEnabled(true);
}
void loopline_handle::onUnlockCarButtonClicked()
{
    QString input = _lockCarEdit->text().trimmed();
    if (input.isEmpty()) {
        statusBar()->showMessage("请输入小车号!");
        return;
    }
    bool ok = false;
    int car_id = input.toInt(&ok);
    if (!ok || car_id <= 0 || car_id > 202)
    {
        statusBar()->showMessage("无效小车号!");
        return;
    }
    _lockCarBtn->setEnabled(false);
    _unlockCarBtn->setEnabled(false);
    _dataProcessMain.unlockCar(car_id);
    _lockCarBtn->setEnabled(true);
    _unlockCarBtn->setEnabled(true);
}
void loopline_handle::clearResults()
{
    if (!_resultsLayout) return;
    // delete all children widgets in the layout
    QLayoutItem* item;
    while ((item = _resultsLayout->takeAt(0)) != nullptr) {
        QWidget* w = item->widget();
        if (w) {
            w->deleteLater();
        }
        else {
            // may be nested layout
            delete item;
        }
    }
    if (_resultsScroll) _resultsScroll->setVisible(false);
}
QWidget* loopline_handle::createQueryPage()	// 单号查询页面
{
    QWidget* w = new QWidget;
    QVBoxLayout* lay = new QVBoxLayout(w);    // 垂直布局

    // 内容靠上排列（关键）
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(8);
    lay->setAlignment(Qt::AlignTop);    // <- 确保子控件从顶部开始布局

    QLabel* title = new QLabel("<b>单号查询</b>");
    title->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    lay->addWidget(title);

    // 中间输入 + 按钮 (水平)
    QWidget* form = new QWidget;
    QHBoxLayout* h = new QHBoxLayout(form);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);

    _queryEdit = new QLineEdit;
    _queryEdit->setPlaceholderText("请输入单号，支持空格/英文逗号/中文逗号分隔");
    _queryBtn = new QPushButton("查询");

    // 保证 form 的垂直高度固定，不被拉伸
    form->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    h->addStretch();
    h->addWidget(_queryEdit);
    h->addWidget(_queryBtn);
    h->addStretch();
    lay->addWidget(form);

    // 回车也触发查询（友好交互）
    connect(_queryBtn, &QPushButton::clicked, this, &loopline_handle::onQueryButtonClicked);
    connect(_queryEdit, &QLineEdit::returnPressed, this, &loopline_handle::onQueryButtonClicked);

    // 结果显示区：用 QScrollArea + 内部 QWidget 的 QVBoxLayout
    _resultsScroll = new QScrollArea;
    _resultsScroll->setWidgetResizable(true);
    _resultsContainer = new QWidget;
    _resultsLayout = new QVBoxLayout(_resultsContainer);
    _resultsLayout->setSpacing(12);
    _resultsLayout->setContentsMargins(6, 6, 6, 6);
    _resultsContainer->setLayout(_resultsLayout);
    _resultsScroll->setWidget(_resultsContainer);

    // 关键：让结果区可扩展并占据剩余空间（垂直方向）
    _resultsScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    lay->addWidget(_resultsScroll);

    // 把 resultsScroll 的 stretch 设置为 1，使其占据主 layout 的剩余空间
    int idx = lay->indexOf(_resultsScroll);
    if (idx >= 0) lay->setStretch(idx, 1);

    // 初始隐藏（没有查询时不显示），但因为我们用 stretch=1，显示后会占据剩余空间
    _resultsScroll->setVisible(false);

    return w;
}
void loopline_handle::onQueryButtonClicked()
{
    QString input = _queryEdit->text().trimmed();
    if (input.isEmpty()) {
        statusBar()->showMessage("请输入查询单号.");
        return;
    }

    // 使用 QRegularExpression（Qt6/Qt5 都可），并且行为使用 Qt::SkipEmptyParts（注意命名空间）
    QStringList tokens = input.split(QRegularExpression(R"([\s,，]+)"), Qt::SkipEmptyParts);

    if (tokens.isEmpty()) {
        statusBar()->showMessage("无有效单号!");
        return;
    }

    _queryBtn->setEnabled(false);
    // 清空旧结果
    clearResults();

    // 对每个 token 查询 DB（同步查询）；若你担心阻塞可以把此整个过程放到 QtConcurrent 中
    auto _sqlManager = SqlConnectionPool::instance().acquire();
    if(!_sqlManager){
        log("----[sql异常] loopline_handle 连接是空指针!");
        return;
    }
    for (const QString& code : tokens) {
        std::string codeStr = code.toStdString();
        auto rowOpt = _sqlManager->queryRowByField("supply_data", "code", codeStr);
        if (rowOpt) {
            auto row = *rowOpt;
            // 获取图片路径（假设数据库 'pic' 表里 path 字段）
            std::optional<std::string> imgOpt = _sqlManager->queryString("pic", "code", codeStr, "path");
            QString imgPath;
            if (imgOpt && !imgOpt->empty()) imgPath = QString::fromStdString(*imgOpt);
            appendResultItem(row, imgPath);
        }
    }
    _resultsContainer->adjustSize();
    _resultsScroll->verticalScrollBar()->setValue(0); // 滚动到顶部（或 setValue(max) 滚到底部）
    _queryBtn->setEnabled(true);

    if (_resultsLayout->count() == 0)
    {
        statusBar()->showMessage(QString("查询失败"));
    }
    else
    {
        int count = _resultsLayout->count() / 2 +1;
        statusBar()->showMessage(QString("查询完成，找到 %1 条记录").arg(count));
    }
}
void loopline_handle::appendResultItem(const std::unordered_map<std::string, std::string>& row, const QString& imagePath)
{
    // 容器 widget：垂直排列（上：一行数据表格；下：图片显示区）
    QWidget* itemWidget = new QWidget;
    QVBoxLayout* itemLay = new QVBoxLayout(itemWidget);
    itemLay->setContentsMargins(4, 4, 4, 4);

    // 上：用一个 QTableView + 单行 model 显示该记录（这样视觉一致）
    QTableView* tv = new QTableView;
    tv->setSelectionBehavior(QAbstractItemView::SelectRows);
    tv->setSelectionMode(QAbstractItemView::ContiguousSelection);
    tv->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tv->verticalHeader()->setVisible(false);
    tv->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tv->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    tv->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tv->setShowGrid(true);
    tv->setGridStyle(Qt::SolidLine);
    tv->setStyleSheet(
        "QTableView {"
        "    gridline-color: #c0c0c0;"        /* 网格线颜色，可按需调整 */
        "}"
        "QHeaderView::section {"
        "    background-color: #cceeff;"     /* 浅蓝色表头 */
        "    padding: 4px;"                  /* 表头内边距 */
        "    font-weight: bold;"             /* 表头加粗 */
        "    border: 1px solid #d0d0d0;"     /* 可选：给表头加边框线 */
        "}"
        );

    QStandardItemModel* model = new QStandardItemModel(this);
    model->setColumnCount(5);
    QStringList headers = { "单号", "重量", "扫描时间", "小车号", "格口号" };
    model->setHorizontalHeaderLabels(headers);

    auto getVal = [&](const std::string& key)->QString {
        auto it = row.find(key);
        if (it != row.end()) return QString::fromStdString(it->second);
        return QString();
    };

    QList<QStandardItem*> rowItems;
    QStandardItem* it0 = new QStandardItem(getVal("code"));
    QStandardItem* it1 = new QStandardItem(getVal("weight"));
    QStandardItem* it3 = new QStandardItem(getVal("scan_time"));
    QStandardItem* it4 = new QStandardItem(getVal("car_id"));
    QStandardItem* it5 = new QStandardItem(getVal("slot_id"));

    for (auto* it : { it0, it1, it3, it4, it5 }) {
        it->setTextAlignment(Qt::AlignCenter);
    }
    rowItems << it0 << it1 << it3 << it4 << it5;
    model->appendRow(rowItems);
    tv->setModel(model);
    // 计算并固定单行高度（可选）
    int h = tv->horizontalHeader()->height() + tv->verticalHeader()->defaultSectionSize() + tv->frameWidth() * 2 + 10;
    tv->setFixedHeight(h);

    itemLay->addWidget(tv);

    QLabel* picLabel = new QLabel;
    picLabel->setAlignment(Qt::AlignCenter);
    picLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    picLabel->setScaledContents(false);
    picLabel->setText(tr("加载图片中..."));

    QScrollArea* picScroll = new QScrollArea;
    picScroll->setWidgetResizable(true);
    picScroll->setFrameStyle(QFrame::NoFrame);
    picScroll->setWidget(picLabel);
    picScroll->setVisible(false); // 初始隐藏，图片加载成功后显示

    itemLay->addWidget(picScroll);

    if (_resultsLayout->count() > 0) {
        QFrame* sep = new QFrame;
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        sep->setStyleSheet("color:#dddddd; margin-top:6px; margin-bottom:6px;");
        _resultsLayout->addWidget(sep);
    }

    // 可选：给 itemWidget 加个白色卡片样式（更美观）
    itemWidget->setStyleSheet(
        "background: #ffffff;"
        "border: 1px solid #ececec;"
        "border-radius: 4px;"
        );

    _resultsLayout->addWidget(itemWidget);

    _resultsScroll->setVisible(true);
    _resultsContainer->updateGeometry();

    // 若有图片路径，则异步加载（用 QImage 在后台）
    if (!imagePath.isEmpty()) {
        QString path = imagePath;
        // start background load
        QFuture<QImage> future = QtConcurrent::run([path]() -> QImage {
            QImage img;
            img.load(path);
            return img;
        });
        QFutureWatcher<QImage>* watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [watcher, picLabel, picScroll, this]() {
            QImage img = watcher->result();
            if (!img.isNull()) {
                // convert to pixmap in main thread
                QPixmap pix = QPixmap::fromImage(img);
                // scale to available width to avoid huge images
                int availW = this->width() * 0.8; // 控制比例
                QSize target = pix.size();
                if (pix.width() > availW) {
                    target = pix.scaled(availW, this->height() * 0.5, Qt::KeepAspectRatio, Qt::SmoothTransformation).size();
                }
                QPixmap scaled = pix.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                picLabel->setPixmap(scaled);
                picLabel->resize(scaled.size());
                picScroll->setMinimumHeight(scaled.height() + 4);
                picScroll->setVisible(true);
            }
            else {
                picLabel->setText(tr("无法加载图片"));
                picScroll->setMinimumHeight(60);
                picScroll->setVisible(true);
            }
            watcher->deleteLater();
        });
        watcher->setFuture(future);
    }
}


