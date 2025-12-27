#ifndef LOOPLINE_HANDLE_H
#define LOOPLINE_HANDLE_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QListWidget>
#include <QSplitter>
#include <unordered_map>
#include <QVector>
#include <QGroupBox>
#include <QTableView>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QCloseEvent>
#include "dataprocessmain.h"
#include "sqlconnectionpool.h"
QT_BEGIN_NAMESPACE
class InitWorker : public QObject
{
    Q_OBJECT
public:
    explicit InitWorker(DataProcessMain* proc, QObject* parent = nullptr)
        : QObject(parent), _proc(proc) {
    }

public slots:
    void doInit()
    {
        try {
            _proc->init();
            emit initFinished(true, "");
        }
        catch (const std::exception& e) {
            emit initFinished(false, e.what());
        }
    }

signals:
    void initFinished(bool ok, const QString& err);

private:
    DataProcessMain* _proc;
};
namespace Ui {
class loopline_handle;
}
QT_END_NAMESPACE

class loopline_handle : public QMainWindow
{
    Q_OBJECT

public:
    loopline_handle(QWidget *parent = nullptr);
    ~loopline_handle();
    void startAsyncInit();
private:
    Ui::loopline_handle *ui;
    void initUI();

    QStackedWidget* _stack = nullptr;
    // 左侧导航
    QListWidget* _navList = nullptr;
    QSplitter* _mainSplitter = nullptr;
    //主界面
    QWidget* _mainPage = nullptr;
    QWidget* createMainPage();
    QGroupBox* createLockGroup();
    QGroupBox* createCarTestGroup();
    QGroupBox* createOpsGroup();
    QTableView* _tableView;
    QWidget* _dashboard = nullptr;
    QMap<int, QLabel*> _machineCountLabels; // 你已有的（示例里用过）
    QMap<int, int> _prevCounts;
    QLineEdit* _driveCarEdit = nullptr;
    QPushButton* _driveCarBtn = nullptr;
    QPushButton* _runCarTestBtn = nullptr;  //驱动小车按顺序循环转动
    QPushButton* _stopCarTestBtn = nullptr; //停止驱动小车按顺序循环转动
    QPushButton* _resetCameraBtn = nullptr; //重置两个相机位置
    QPushButton* _resetSlotConfigBtn = nullptr;   //重置格口位置按钮
    QPushButton* _testCameraBtn = nullptr;//测试相机
    QPushButton* _resetDriverBtn = nullptr; //重置驱动信息
    QLineEdit* _lockCarEdit = nullptr;  //解锁/加锁小车号
    QPushButton* _lockCarBtn = nullptr;
    QPushButton* _unlockCarBtn = nullptr;
    //查询界面
    QWidget* createQueryPage();
    void clearResults();
    void appendResultItem(const std::unordered_map<std::string, std::string>& row, const QString& imagePath);
    QWidget* _queryPage = nullptr;
    QTableView* _queryTable;
    QLineEdit *_queryEdit = nullptr;
    QPushButton* _queryBtn = nullptr;
    QStandardItemModel* _queryModel = nullptr;
    QScrollArea* _imgScroll = nullptr;
    QLabel* _imgLabel = nullptr;
    QScrollArea* _resultsScroll = nullptr;        // 顶层滚动区，包含 results container
    QWidget* _resultsContainer = nullptr;         // scroll 的内容 widget
    QVBoxLayout* _resultsLayout = nullptr;        // 容器中的垂直布局，用来追加 result item
    //ui交互
    std::atomic<bool> _isDrivingCarTest{ false }; // 防止重复点击启动小车

    void log(const std::string& Message)
    {
        Logger::getInstance().Log(Message);
    }

    DataProcessMain _dataProcessMain;

private slots:
    //主界面事件
    void onDriveCarButtonClicked();
    void onRunCarTestButtonClicked();   //驱动小车按顺序循环转动
    void onStopCarTestButtonClicked();  //停止驱动小车按顺序循环转动
    void onLockCarButtonClicked();
    void onUnlockCarButtonClicked();
    //查询界面
    void onQueryButtonClicked();     // 查询按钮点击处理
protected:
    void closeEvent(QCloseEvent *event) override
    {
        // qDebug() << "---- 点击了关闭按钮";
        try{
            _dataProcessMain.cleanup();
            Logger::getInstance().close();
            SqlConnectionPool::instance().shutdown();
        }catch(...){}

        // 接受关闭（真正关闭窗口）
        event->accept();
    }
};
#endif // LOOPLINE_HANDLE_H







