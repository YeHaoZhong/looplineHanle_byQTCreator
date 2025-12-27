#ifndef DEVICEMANAGER_H
#define DEVICEMANAGER_H
#include <QObject>
#include "socketclinet.h"
#include "logger.h"
#include <shared_mutex>
#include "StructInfo.h"
#include "plccontrol.h"
#include "caritemswritethread.h"
#include <QFutureWatcher>

class DeviceManager : public QObject {
    Q_OBJECT
public:
    DeviceManager();
    ~DeviceManager();

    void init();
    void dbInit();
    void tcpConnection();
    void updateCarPosition();
    void carLoop();	//循环遍历下件
    void handleCarUnload(int car_id, bool direction, std::string code, int slot_id);
    int getTimeDiff();
    void initCarPosition(int car_id, int current_position);
    void initCarItems(int car_id);
    void log(const std::string& message)
    {
        Logger::getInstance().Log(message);
    }
    void startLoop();
    void stopLoop();
    void updateSlotByCarID(int car_id, int slot_id);	//请求后设置对应小车的格口号
    void updateSlotByCode(const std::string& code, int slot_id);	//通过面单号查找对应小车, 并设置小车的格口号
    void updateSlotConfig();
    void slotLoop();

    void serialPortInit();
    bool driveByCarID( int car_id, bool Corotation = true,int speed = 90, int distance = 80, int acceleration = 100);    //Corotation = true 是正转? mode = 0 上件, mode = 1下件
    void updateCarForCamera();	//更新当前应拍摄相机的小车id
    int carToCamera41();	//返回当前应拍摄41相机的小车id
    int carToCamera42();	//返回当前应拍摄42相机的小车id

    void updateCodeToCarMap(const std::string& code, int car_id);	//将面单号与小车号绑定
    void testCarLoop();
    void startCarTestLoop();
    void stopCarTestLoop();
    void resetCameraPositions();
    void resetSlotConfigurations();
    void testCamera();
    void resetDriver();
    void lockCar(int car_id);
    void unlockCar(int car_id);

    void tcp_disconnection();
    void cleanup();
private:
    std::atomic<bool> carLoop_readCarItems{true};	//在经过一次光电后才进行读取一次小车上所有的信息状态
    std::atomic<bool> carLoop_readCarStatus{true};

    SocketConnection _stepTcp;
    SocketConnection _headTcp;
    SocketConnection _emptyTcp;

    bool IsUpLayerLine = true;
    std::atomic<int64_t> lastStepTimeNs{ 0 }; //上一次步进时间纳秒
    std::atomic<int> passingCarNum{ 0 }; //经过的小车数量
    std::atomic<int> oneCarTime{ 0 };    //一个小车的时间,一直变化
    int TotalCarNum, TotalPortNum;
    std::string main_plc_ip = "192.168.93.52";
    std::atomic<int> originSignalCount{ 0 }; //头车感应次数
    std::atomic<int64_t> lastOriginTimeNs{ 0 }; //上一次头车感应时间纳秒

    std::vector<CarInfo> carStatus;     //小车位置
    std::shared_mutex carStatusLock;   //多读少写, 线程锁

    std::vector<CarItem> carItems;   //小车扩展状态及信息
    std::shared_mutex carItemsLock;

    std::unique_ptr<CarItemsWriteThread> carItemsWriter;	//写入
    std::unordered_map<int, OutPortInfo> outports_map;    //格口位置

    PlcControl _s7QueryPlcSlot;
    std::unordered_map<int, bool> slots_status_map;	//格口状态 true = 锁格口, false = 可下件
    std::shared_mutex slotsStatus_lock;

    bool m_polling = false;
    std::thread m_slotThread;
    std::thread m_pollCarThread;

    std::atomic<bool> m_test{ false };
    std::thread m_testCarThread;

    std::atomic<int> test_slot_id{ 0 };	//强排口

    int serialPortCount = 9;	//总端口数量(一个串口服务器两个端口)
    int seqNum = 0;
    static constexpr int CarsPerSocket = 24;	//串口服务器一个端口控制24个小车
    std::vector<QPointer<SocketClient>> SerialSockets;	//串口服务器连接
    std::vector<std::unique_ptr<std::mutex>> _serialSocketsLock;
    std::vector<QPointer<SocketClient>> secondSerialSockets;    //备用服务器连接

    std::unordered_map<std::string, int> codeToCarMap;
    std::shared_mutex _codeToCarLock;

    SocketConnection _cameraClient41;	//ip为 41相机, 端口2001
    SocketConnection _cameraClient42;	//ip为 42相机, 端口2002
    int camera41_send_port,camera42_send_port;
    std::string camera41_ip = "";
    std::string camera42_ip = "";
    std::atomic<int> _camera41Position{ 80 };
    std::atomic<int> _camera42Position{ 113 };
    std::atomic<int> _currentCarIdFor41{ 1 };	//41相机对应下的小车id
    std::atomic<int> _currentCarIdFor42{ 1 };	//42相机对应下的小车id

    std::atomic<int> _car_speed_first{ 1 };
    std::atomic<int> _car_distance_first{ 1 };
    std::atomic<int> _car_acceleration_first{ 1 };
    std::atomic<int> _car_speed_second{ 1 };
    std::atomic<int> _car_distance_second{ 1 };
    std::atomic<int> _car_acceleration_second{ 1 };

    std::vector<bool> carLocks;
    std::atomic<bool> carLoop_readCarLocks{ true };

    bool isTCPConnectOver = false;
    bool isSerialPortConnectOver = false;

private slots:
    void stepReceive(const QByteArray& data);
    void headReceive(const QByteArray& data);
    void emptyReceive(const QByteArray& data);
    void handleCommandSent(int socketIndex, bool success);    //处理发送串口服务器指令结果
    void onSerialDataReceived(int socketIndex, const QByteArray& data);
signals:
    void driveCommandToIndex(int index, QByteArray command);
};


#endif // DEVICEMANAGER_H
