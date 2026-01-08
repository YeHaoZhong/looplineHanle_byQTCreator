#include "devicemanager.h"
#include <QTimer>
#include <QtConcurrent>   // QtConcurrent::run
#include <QPointer>       // QPointer
#include <QMetaObject>    // QMetaObject::invokeMethod
#include "sqlconnectionpool.h"
#include "steplogger.h"
#include <sstream>

DeviceManager::DeviceManager()
{
    TotalCarNum = 202;
    TotalPortNum = 252;
    camera41_send_port = 0;
    camera42_send_port = 0;
    _currentCarIdFor41.store(1,std::memory_order_release);
    _currentCarIdFor42.store(1,std::memory_order_release);
}
void DeviceManager::init()
{
    try
    {
        dbInit();
        carItemsWriter.reset(new CarItemsWriteThread(carItems, carItemsLock));
        carItemsWriter->startLoop();
        Sleep(20);
        tcpConnection();
        connect(&_stepTcp.client, &SocketClient::dataReceived, this, &DeviceManager::stepReceive);      //步进光电
        connect(&_headTcp.client, &SocketClient::dataReceived, this, &DeviceManager::headReceive);      //头车光电
        connect(&_emptyTcp.client, &SocketClient::dataReceived, this, &DeviceManager::emptyReceive);    //空车光电
        serialPortInit();
        startLoop();
    }
    catch (const std::exception& e)
    {
        log("---- [设备初始化] 初始化异常 : " + std::string(e.what()));
    }
}
void DeviceManager::dbInit()
{
    auto _sqlQuery = SqlConnectionPool::instance().acquire();
    if(!_sqlQuery){
        log("----[sql异常] DeviceManager 连接是空指针!");
        return;
    }
    try
    {
        auto plc_ip = _sqlQuery->queryString("config", "name", "main_plc_ip", "value");
        if (plc_ip)
        {
            main_plc_ip = *plc_ip;
        }
        auto car_num = _sqlQuery->queryString("config", "name", "car_num", "value");
        if (car_num)
        {
            TotalCarNum = std::stoi(*car_num);
            // carPosition_first.resize(48);
            // carPosition_second.resize(48);
            // carPosition_thrid.resize(48);
            // carPosition_fourth.resize(48);
            // carPosition_fifth.resize(10);
            carStatus.resize(TotalCarNum);
            carItems.resize(TotalCarNum);
            carLocks.resize(TotalCarNum);
            for (int i = 1; i <= TotalCarNum; ++i)
            {
                initCarPosition(i, i);//初始化小车位置, 小车号就是当前位置, 内部加锁
                initCarItems(i);//初始化小车上信息
            }
        }
        auto position41 = _sqlQuery->queryString("config", "name", "camera_position_one", "value");
        if (position41)
        {
            _camera41Position = std::stoi(*position41);
        }
        auto position42 = _sqlQuery->queryString("config", "name", "camera_position_two", "value");
        if (position42)
        {
            _camera42Position = std::stoi(*position42);
        }
        auto camera41_port = _sqlQuery->queryString("config", "name", "camera_send_one", "value");
        if (camera41_port)
        {
            camera41_send_port = std::stoi(*camera41_port);
        }
        auto camera42_port = _sqlQuery->queryString("config", "name", "camera_send_two", "value");
        if(camera42_port)
        {
            camera42_send_port = std::stoi(*camera42_port);
        }
        auto db_camera41_ip = _sqlQuery->queryString("config", "name", "camera_ip_one", "value");
        if (db_camera41_ip)	camera41_ip = *db_camera41_ip;

        auto db_camera42_ip = _sqlQuery->queryString("config", "name", "camera_ip_two", "value");
        if (db_camera42_ip)	camera42_ip = *db_camera42_ip;

        auto slot_config = _sqlQuery->readTable("outport_config");
        if (!slot_config.empty())
        {
            //TotalPortNum = slot_config.size();
            TotalPortNum = 252;
            for (const auto& row : slot_config)
            {
                if (row.size() < 4)continue;
                int port_id = std::stoi(row[0]);
                int position = std::stoi(row[1]);
                int offset = std::stoi(row[2]);
                bool inside = (row[3] == "1");
                outports_map[port_id] = { port_id, position, offset, inside };
                slots_status_map[port_id] = false;//初始化格口状态, 0 = 正常, 1 = 锁格
            }
        }
        auto strong_slot_config = _sqlQuery->queryString("config", "name", "strong_slot", "value");
        if (strong_slot_config)
        {
            test_slot_id.store(std::stoi(*strong_slot_config));
            log("---- [初始化] 强排口: [" + *strong_slot_config + "]");
        }
        auto car_speed_first = _sqlQuery->queryString("config", "name", "car_speed_first", "value");
        if (car_speed_first) _car_speed_first.store(std::stoi(*car_speed_first));
        auto car_distance_first = _sqlQuery->queryString("config", "name", "car_distance_first", "value");
        if (car_distance_first) _car_distance_first.store(std::stoi(*car_distance_first));
        auto car_acceleration_first = _sqlQuery->queryString("config", "name", "car_acceleration_first", "value");
        if (car_acceleration_first) _car_acceleration_first.store(std::stoi(*car_acceleration_first));
        log("---- [初始化] 运行速度:[" + std::to_string(_car_speed_first.load()) + "], 运行距离:[" + std::to_string(_car_distance_first.load()) + "], 运行加速度:[" + std::to_string(_car_acceleration_first) + "]");

        auto db_camera41_sendcarid_ip = _sqlQuery->queryString("config","name","camera_carid_ip_one","value");
        if(db_camera41_sendcarid_ip) camera41_carid_ip = *db_camera41_sendcarid_ip;
        auto db_camera41_sendcarid_port = _sqlQuery->queryString("config","name","camera_carid_port_one","value");
        if(db_camera41_sendcarid_port) camera41_send_carid_port = std::stoi(*db_camera41_sendcarid_port);

        auto db_camera42_sendcarid_ip = _sqlQuery->queryString("config","name","camera_carid_ip_two","value");
        if(db_camera42_sendcarid_ip) camera42_carid_ip = *db_camera42_sendcarid_ip;
        auto db_camera42_sendcarid_port = _sqlQuery->queryString("config","name","camera_carid_port_two","value");
        if(db_camera42_sendcarid_port) camera42_send_carid_port = std::stoi(*db_camera42_sendcarid_port);
    }
    catch (const std::exception& e)
    {
        log("---- [初始化] 异常 : " + std::string(e.what()));
    }
}
void DeviceManager::serialPortInit()        //不使用一个线程一个连接, 直接在主线程中的连接池
{
    auto _sqlQuery = SqlConnectionPool::instance().acquire();
    if(!_sqlQuery){
        log("----[sql异常] DeviceManager 连接是空指针!");
        return;
    }
    auto host_vec = _sqlQuery->readTable("serial_server_config");
    if (host_vec.empty() || host_vec.size() != serialPortCount) {
        log("---- [错误] serial_server_config invalid");
        return;
    }

    if (SerialSockets.size() < static_cast<size_t>(serialPortCount))
    {
        SerialSockets.resize(serialPortCount);
        _serialSocketsLock.reserve(serialPortCount);
        secondSerialSockets.resize(serialPortCount);
    }

    for (int i = 0; i < serialPortCount; ++i) {
        _serialSocketsLock.emplace_back(std::make_unique<std::mutex>());
        // 已连接则跳过
        if (!SerialSockets[i].isNull() && SerialSockets[i]->SocketConnection)
            continue;

        const std::string host = host_vec[i][1];
        const int port = std::stoi(host_vec[i][2]);

        SocketClient* conn = new SocketClient();   // 不设 parent（可选）
        try {
            SOCKET ok = conn->ConnectTo(host, port);
            if (ok != INVALID_SOCKET) {
                log("---- [Initialize] Serial server connected! index: ["
                    + std::to_string(i) + "] IP: [" + host + "], 连接成功!");

                // 接收数据 → 主线程处理
                connect(conn, &SocketClient::dataReceived,
                        this, [this, i](const QByteArray& data) {
                            onSerialDataReceived(i, data);
                        });

                // 发送命令（主线程调用，SocketClient 内部 send）
                connect(this, &DeviceManager::driveCommandToIndex,
                        conn, &SocketClient::sendComand,
                        Qt::DirectConnection);
            }
            else {
                log("---- [Error] Serial server connect failed! index: ["
                    + std::to_string(i) + "] IP: [" + host + "], 连接失败!");
            }
        }
        catch (const std::exception& e) {
            log("---- [Exception] Serial connect exception index ["
                + std::to_string(i) + "] : " + e.what());
        }
        SerialSockets[i] = QPointer<SocketClient>(conn);
        Sleep(30);
    }
    // for (int i = 0; i < serialPortCount; ++i) {             //备用连接初始化

    //     // 已连接则跳过
    //     if (!secondSerialSockets[i].isNull() && secondSerialSockets[i]->SocketConnection)
    //         continue;

    //     const std::string host = host_vec[i][1];
    //     const int port = std::stoi(host_vec[i][2]);

    //     SocketClient* conn = new SocketClient();   // 不设 parent（可选）
    //     try {
    //         SOCKET ok = conn->ConnectTo(host, port);
    //         if (ok != INVALID_SOCKET) {
    //             log("---- [Initialize] Second Serial server connected! index: ["
    //                 + std::to_string(i) + "] IP: [" + host + "],连接成功!");

    //             // 接收数据 → 主线程处理
    //             connect(conn, &SocketClient::dataReceived,
    //                     this, [this, i](const QByteArray& data) {
    //                         onSerialDataReceived(i, data);
    //                     });

    //             // 发送命令（主线程调用，SocketClient 内部 send）
    //             connect(this, &DeviceManager::driveCommandToIndex,
    //                     conn, &SocketClient::sendComand,
    //                     Qt::DirectConnection);
    //         }
    //         else {
    //             log("---- [Error] Second Serial server connect failed! index: ["
    //                 + std::to_string(i) + "] IP: [" + host + "], 连接失败! ");
    //         }
    //     }
    //     catch (const std::exception& e) {
    //         log("---- [Exception] Second Serial connect exception index ["
    //             + std::to_string(i) + "] : " + e.what());
    //     }
    //     secondSerialSockets[i] = QPointer<SocketClient>(conn);
    //     Sleep(30);
    // }
}
void DeviceManager::handleCommandSent(int socketIndex, bool success)
{
    // 处理发送结果
    if (success) {
        log("---- [串口服务器] 命令发送至 [" + std::to_string(socketIndex) + "]");
    }
    else {
        log("---- [Error] Command failed to send to serial socket index [" + std::to_string(socketIndex) + "]");
    }
}
void DeviceManager::onSerialDataReceived(int socketIndex, const QByteArray& data)
{
    QString hex = data.toHex(' ').toUpper();
    //log("---- [小车回码] 第 [" + std::to_string(socketIndex) + "] 个端口, 回码数据: [" + hex.toStdString() + "]");
}
bool DeviceManager::driveByCarID(int car_id,
                                 uint64_t lastCarPositionVersion,
                                 bool Corotation,
                                 int speed,
                                 int distance,
                                 int acceleration
                                 )    //Corotation = true 是正转? mode = 0 上件, mode = 1下件
{
    try
    {
        if (car_id < 1 || car_id>TotalCarNum) {
            log("---- [错误] driveByCarID: 小车号 [" + std::to_string(car_id) + "] 不存在!");
            return false;
        }
        std::array<uint8_t, 8> data{};
        data[0] = 0x84;
        int servialCarID = ((car_id - 1) % CarsPerSocket) + 1; //获取在串口服务器的编号
        data[1] = Corotation ? static_cast<uint8_t>(servialCarID) : static_cast<uint8_t>(servialCarID + 0x40);
        data[2] = static_cast<uint8_t>(_car_speed_first.load());
        data[3] = 0x00;
        data[4] = static_cast<uint8_t>(_car_distance_first.load());
        data[5] = 0x00;
        data[6] = static_cast<uint8_t>(_car_acceleration_first.load());
        uint8_t checksum = 0;
        for (int i = 1; i <= 6; ++i)
        {
            checksum ^= data[i];
        }
        data[7] = checksum;
        std::string commandString = std::string(reinterpret_cast<char*>(data.data()), data.size());

        QByteArray command = QByteArray::fromStdString(commandString);

        int index = (car_id - 1) / CarsPerSocket;
        // 边界校验...
        if (index < 0 || index >= static_cast<int>(SerialSockets.size()))
        {
            log("---- [错误] driveByCarID: 串口服务器索引 [" + std::to_string(index) + "] 超出数量范围!");
            return false;
        }
        auto connPtr = SerialSockets[index].get();
        if(connPtr == nullptr || !connPtr->SocketConnection)
        {
            log("---- [错误] driveByCarID: 串口服务器索引 [" + std::to_string(index) + "] 未连接!");
            return false;
        }
        connPtr->sendComand(index, command);
        QString commandQStr = command.toHex(' ').toUpper();
        log("---- [命令帧] 发送: [" + commandQStr.toStdString() + "] 至第 [" + std::to_string(index + 1) + "] 个TCP端口, [" + std::to_string(car_id) + "] 小车运动!");
        return true;
    }
    catch (const std::exception& e)
    {
        log("---- [错误] driveByCarID Exception: " + std::string(e.what()));
        return false;
    }
}

void DeviceManager::startLoop()
{
    m_polling = true;
    m_pollCarThread = std::thread(&DeviceManager::carLoop, this);     //小车位置轮询线程
    m_slotThread = std::thread(&DeviceManager::slotLoop, this);
    // m_pollCarFirstThread = std::thread(&DeviceManager::carLoop_first,this);
    // m_pollCarSecondThread = std::thread(&DeviceManager::carLoop_second,this);
    // m_pollCarThirdThread = std::thread(&DeviceManager::carLoop_third,this);
    // m_pollCarFourthThread = std::thread(&DeviceManager::carLoop_fourth,this);
    // m_pollCarFifthThread = std::thread(&DeviceManager::carLoop_fifth,this);
}
void DeviceManager::stopLoop()
{
    m_polling = false;
    if (m_slotThread.joinable())
    {
        m_slotThread.join();
    }
    if (m_pollCarThread.joinable())
    {
        m_pollCarThread.join();
    }
    // if(m_pollCarFirstThread.joinable()){
    //     m_pollCarFirstThread.join();
    // }
    // if(m_pollCarSecondThread.joinable()){
    //     m_pollCarSecondThread.join();
    // }
    // if(m_pollCarThirdThread.joinable()){
    //     m_pollCarThirdThread.join();
    // }
    // if(m_pollCarFourthThread.joinable()){
    //     m_pollCarFourthThread.join();
    // }
    // if(m_pollCarFifthThread.joinable()){
    //     m_pollCarFifthThread.join();
    // }
}
void DeviceManager::startCarTestLoop()
{
    try
    {
        if (m_test.load() == true) return;
        m_test.store(true);
        m_testCarThread = std::thread(&DeviceManager::testCarLoop, this);
    }
    catch (const std::exception& e)
    {
        log("---- [startCarTestLoop] 异常: " + std::string(e.what()));
    }
}
void DeviceManager::stopCarTestLoop()
{
    try
    {
        if (m_test.load() == false) return;
        m_test.store(false);
        if (m_testCarThread.joinable())
        {
            m_testCarThread.join();
        }
    }
    catch (const std::exception& e)
    {
        log("---- [stopCarTestLoop] 异常: " + std::string(e.what()));
    }
}
void DeviceManager::testCarLoop()
{
    while (m_test.load())
    {
        for (int i = 1; i < 203; i++)
        {
            driveByCarID(i, carLoop_readCarStatusVersion.load(std::memory_order_acquire));
            if (m_test.load() == false) break;
            Sleep(500);
        }
        Sleep(2000);
    }
}
void DeviceManager::tcpConnection()
{
    if (main_plc_ip == "") return;
    try
    {
        if (_headTcp.connectTo(main_plc_ip, 2000))
        {
            log("---- [头车光电] TCP连接成功!");
        }
        else
        {
            log("---- [头车光电] TCP连接失败!");
        }
        Sleep(20);
        if (_stepTcp.connectTo(main_plc_ip, 2001))
        {
            log("---- [步进光电] TCP连接成功!");
        }
        else
        {
            log("---- [步进光电] TCP连接失败!");
        }
        Sleep(20);
        if (_emptyTcp.connectTo(main_plc_ip, 2002))
        {
            log("---- [空车光电] TCP连接成功!");
        }
        else
        {
            log("---- [空车光电] TCP连接失败!");
        }
        Sleep(20);
        if (_s7QueryPlcSlot.ConnectToPLC(main_plc_ip, 0, 1)) {
            log("---- [S7连接] 连接成功!");
        }
        else    log("---- [S7连接] 连接失败!");
        // Sleep(20);
        // if (_cameraClient41.connectTo(camera41_ip, camera41_send_port))	log("---- [41相机] 触发端口连接成功!");
        // else    log("---- [41相机] 发送端口连接失败!");
        // Sleep(20);
        // if (_cameraClient42.connectTo(camera42_ip, camera42_send_port))	log("---- [42相机] 触发端口连接成功!");
        // else    log("---- [42相机] 发送端口连接失败!");
        // Sleep(20);
        // if(_cameraSendCarId41.connectTo(camera41_carid_ip,camera41_send_carid_port)) log("---- [41相机] 发送小车端口连接成功!");
        // else log("---- [41相机] 发送小车端口连接失败!");
        // Sleep(10);
        // if(_cameraSendCarId42.connectTo(camera42_carid_ip,camera42_send_carid_port)) log("---- [42相机] 发送小车端口连接成功!");
        // else log("---- [42相机] 发送小车端口连接失败!");
    }
    catch (const std::exception& e)
    {
        if (IsUpLayerLine)   log("---- [up Error] PlcSocket connection exception: " + std::string(e.what()));
        else log("---- [down Error] PlcSocket connection exception: " + std::string(e.what()));
    }
    isTCPConnectOver = true;

}
DeviceManager::~DeviceManager()
{

}
void DeviceManager::cleanup()
{
    stopLoop();
    tcp_disconnection();
    carItemsWriter.reset();
}
void DeviceManager::tcp_disconnection()
{
    try
    {
        _headTcp.disconnect();
        _stepTcp.disconnect();
        _emptyTcp.disconnect();
        // _cameraClient41.disconnect();
        // _cameraClient42.disconnect();
        _s7QueryPlcSlot.DisconnectFromPLC();
        // _cameraSendCarId41.disconnect();
        for (int i = 0; i < serialPortCount; ++i)
        {
            SerialSockets[i].get()->disconnect();
            SerialSockets[i].clear();
            secondSerialSockets[i].get()->disconnect();
            secondSerialSockets[i].clear();
            _serialSocketsLock[i].reset();
        }
    }
    catch (...)
    {

    }
}
void DeviceManager::updateCodeToCarMap(const std::string& code, int car_id)
{
    try
    {
        std::string copy_code = code;
        int copy_car_id = car_id;
        if(copy_car_id<1||copy_car_id>TotalCarNum){
            log("---- [updateCodeToCarMap] 单号:["+code+"], 小车号超出索引范围! 小车号: ["+std::to_string(car_id)+"]");
            return;
        }
        int vector_car_id = car_id - 1;
        const std::string& item_code = carItems[vector_car_id].code;        //因为是顺着小车走, 不会短时间访问同一个小车, 不加锁

        if(item_code == code){                              //小车上的单号是同一个, 不进行写入
            return;
        }

        std::unique_lock<std::shared_mutex> writelock(_codeToCarLock, std::try_to_lock);
        if(writelock.owns_lock()){
            // codeToCarMap[copy_code] = copy_car_id;
            codeToCarMap.insert_or_assign(copy_code, copy_car_id);
            writelock.unlock();
        }
        carItemsWriter->writeItemInfo(copy_car_id, copy_code);
    }
    catch (const std::exception& e)
    {
        log("---- [updateCodeToCarMap] 异常: " + std::string(e.what()));
    }
}
void DeviceManager::updateSlotByCarID(int car_id, int slot_id)     //设置小车对应的格口号
{
    try
    {
        int copy_slot_id = slot_id;
        int copy_car_id = car_id;
        if (copy_car_id <1 || copy_car_id>TotalCarNum) return;
        if (copy_slot_id<1 || copy_slot_id>TotalPortNum)    //掉异常口
        {
            copy_slot_id = test_slot_id.load();
        }
        int position = outports_map[copy_slot_id].position;
        int offset = outports_map[copy_slot_id].offset;
        bool inside = outports_map[copy_slot_id].inside;
        int vector_car_id = copy_car_id - 1;

        std::shared_lock<std::shared_mutex> readlock(carItemsLock);
        int item_slot_id = carItems[vector_car_id].port_num;
        readlock.unlock();

        if (item_slot_id == copy_slot_id) return;       //格口没有变化, 不更新
        carItemsWriter->writeSlotInfo(copy_car_id, copy_slot_id, position, offset, inside);
        carLoop_readCarItemsVersion.fetch_add(1,std::memory_order_release);         //20260101修改
        // carLoop_readCarItems.store(true);   //更新了item中下件信息, carloop中需要读取
    }
    catch (const std::exception& e)
    {
        if (IsUpLayerLine)   log("---- [up setCarSlot] Exception: " + std::string(e.what()));
        else    log("---- [down setCarSlot] Exception: " + std::string(e.what()));
    }
}
void DeviceManager::updateSlotByCode(const std::string& code, int slot_id)
{
    try
    {
        std::string copy_code = code;
        int copy_slot_id = slot_id;
        std::shared_lock<std::shared_mutex> readlock(_codeToCarLock);
        auto it = codeToCarMap.find(copy_code);
        if (it != codeToCarMap.end())
        {
            int car_id = it->second;
            readlock.unlock();
            codeToCarMap.erase(it);
            updateSlotByCarID(car_id, copy_slot_id);
        }
        else
        {
            readlock.unlock();
        }
    }
    catch (const std::exception& e)
    {
        log("---- [更新格口] 通过面单号查找对应小车进行更新格口异常: " + std::string(e.what()));
    }
}
void DeviceManager::updateCarForCamera()
{
    try
    {
        int current_passingCar = passingCarNum;
        int carForCamera41 = ((current_passingCar + _camera41Position) - 1) % TotalCarNum + 1;
        int carForCamera42 = ((current_passingCar + _camera42Position) - 1) % TotalCarNum + 1;

        step_camera41Count.fetch_add(1,std::memory_order_release);              //41相机计数累加
        _currentCarIdFor41.store(carForCamera41,std::memory_order_release);     //修改当前对应车号, 加入std::memory_order_release 确保读取到的数据一致性

        step_camera42Count.fetch_add(1,std::memory_order_release);              //42相机计数累加
        _currentCarIdFor42.store(carForCamera42,std::memory_order_release);
        // _currentCarIdFor42 = carForCamera42;
    }
    catch (const std::exception& e)
    {
        log("---- [相机位置] 更新小车号异常: " + std::string(e.what()));
    }
}
std::tuple<uint64_t,int> DeviceManager::carToCamera41()  //返回当前相机41的计数以及对应小车号
{
    return std::make_tuple(step_camera41Count.load(std::memory_order_acquire),_currentCarIdFor41.load(std::memory_order_acquire));
}
std::tuple<uint64_t,int> DeviceManager::carToCamera42()  //返回当前相机42的计数以及对应小车号
{
    return std::make_tuple(step_camera42Count.load(std::memory_order_acquire),_currentCarIdFor42.load(std::memory_order_acquire));
}
void DeviceManager::updateCamera41Count(uint64_t new_count){
    step_camera41Count.store(new_count,std::memory_order_release);
}
void DeviceManager::updateCamera42Count(uint64_t new_count){
    step_camera42Count.store(new_count,std::memory_order_release);
}
void DeviceManager::stepReceive(const QByteArray& data) //步进接收
{
    try
    {
        QByteArray hex = data.toHex();
        bool ok = false;
        int passingCar = hex.toULongLong(&ok, 16);
        passingCarNum = passingCar;
        auto nowTp = std::chrono::steady_clock::now().time_since_epoch();   //当前触发步进时间
        int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(nowTp).count();    //转化为纳秒
        updateCarPosition();    //更新全局小车状态
        carLoop_readCarStatusVersion.fetch_add(1,std::memory_order_release);
        updateCarForCamera();
        lastStepTimeNs.store(nowNs);

        // std::ostringstream oss;
        // oss << std::setw(3) << std::setfill('0') << _currentCarIdFor41.load(std::memory_order_acquire);
        // std::string send41_carid = oss.str();
        // if(_cameraSendCarId41.isConnected)
        // {
        //     _cameraSendCarId41.send(send41_carid);
        // }
        StepLogger::getInstance().Log("---- [步进光电] 接收数据: ["+ std::to_string(passingCar)
                                      + "], 6200当前小车号: ["+std::to_string(_currentCarIdFor42.load(std::memory_order_acquire))
                                      + "], 6089当前小车号: ["+std::to_string(_currentCarIdFor41.load(std::memory_order_acquire))+"]");
    }
    catch (const std::exception& ex)
    {
        log("---- [步进光电] 回传处理异常:" + std::string(ex.what()));
    }
}
void DeviceManager::updateCarPosition()
{
    try
    {
        std::unique_lock<std::shared_mutex> carStatus_writeLock(carStatusLock);
        for (auto& car_info : carStatus)
        {
            int vector_carid = car_info.carID - 1;
            int currentPosition = (TotalCarNum - passingCarNum + car_info.carID) % TotalCarNum;  //计算当前车的位置, vector中实际小车号从1开始,实际点位从1开始
            carStatus[vector_carid].currentPosition = currentPosition;   //更新小车位置
        }
        carStatus_writeLock.unlock();
    }
    catch (const std::exception& e)
    {
        log("---- [小车位置] 更新异常: " + std::string(e.what()));
    }
    // try
    // {
    //     std::unique_lock<std::shared_mutex> carStatus_writeLock_first(carPositionLock_first);         //1-48
    //     for (auto& car_info : carPosition_first)
    //     {
    //         int vector_carid = car_info.carID - 1;
    //         int currentPosition = (TotalCarNum - passingCarNum + car_info.carID) % TotalCarNum;  //计算当前车的位置, vector中实际小车号从1开始,实际点位从1开始
    //         carPosition_first[vector_carid].currentPosition = currentPosition;   //更新小车位置
    //     }
    //     carStatus_writeLock_first.unlock();
    //     // carLoop_readPositionVer_first.fetch_add(1,std::memory_order_release);

    //     std::unique_lock<std::shared_mutex> carStatus_writeLock_second(carPositionLock_second);         //49-96
    //     for (auto& car_info : carPosition_second)
    //     {
    //         int vector_carid = car_info.carID - 1;
    //         int currentPosition = (TotalCarNum - passingCarNum + car_info.carID) % TotalCarNum;  //计算当前车的位置, vector中实际小车号从1开始,实际点位从1开始
    //         carPosition_second[vector_carid].currentPosition = currentPosition;   //更新小车位置
    //     }
    //     carStatus_writeLock_second.unlock();
    //     // carLoop_readPositionVer_second.fetch_add(1,std::memory_order_release);

    //     std::unique_lock<std::shared_mutex> carStatus_writeLock_third(carPositionLock_third);         //
    //     for (auto& car_info : carPosition_thrid)
    //     {
    //         int vector_carid = car_info.carID - 1;
    //         int currentPosition = (TotalCarNum - passingCarNum + car_info.carID) % TotalCarNum;  //计算当前车的位置, vector中实际小车号从1开始,实际点位从1开始
    //         carPosition_thrid[vector_carid].currentPosition = currentPosition;   //更新小车位置
    //     }
    //     carStatus_writeLock_third.unlock();

    //     std::unique_lock<std::shared_mutex> carStatus_writeLock_fourth(carPositionLock_fourth);         //
    //     for (auto& car_info : carPosition_fourth)
    //     {
    //         int vector_carid = car_info.carID - 1;
    //         int currentPosition = (TotalCarNum - passingCarNum + car_info.carID) % TotalCarNum;  //计算当前车的位置, vector中实际小车号从1开始,实际点位从1开始
    //         carPosition_fourth[vector_carid].currentPosition = currentPosition;   //更新小车位置
    //     }
    //     carStatus_writeLock_fourth.unlock();

    //     std::unique_lock<std::shared_mutex> carStatus_writeLock_fifth(carPositionLock_fifth);         //
    //     for (auto& car_info : carPosition_fifth)
    //     {
    //         int vector_carid = car_info.carID - 1;
    //         int currentPosition = (TotalCarNum - passingCarNum + car_info.carID) % TotalCarNum;  //计算当前车的位置, vector中实际小车号从1开始,实际点位从1开始
    //         carPosition_fifth[vector_carid].currentPosition = currentPosition;   //更新小车位置
    //     }
    //     carStatus_writeLock_fifth.unlock();
    // }
    // catch (const std::exception& e)
    // {
    //     log("---- [小车位置] 更新异常: " + std::string(e.what()));
    // }
}
void DeviceManager::headReceive(const QByteArray& data)  //有货的小车转两圈后强制下格口
{
    try
    {
        originSignalCount.fetch_add(1);   //头车感应次数累加
    }
    catch (const std::exception& ex) {
        log("---- [头车光电] 数据处理异常: " + std::string(ex.what()));
    }
}
void DeviceManager::emptyReceive(const QByteArray& data)    //空车接收
{
    try
    {
        QByteArray hex = data.toHex();           // e.g. "01A3FF"
        bool ok = false;
        int car_id = hex.toULongLong(&ok, 16);
        int vector_carid = car_id - 1;
        if (car_id<1 || car_id>TotalCarNum)  return;  //小车号不合法
        std::shared_lock<std::shared_mutex> carItem_rLock(carItemsLock);
        bool is_fault = carItems[vector_carid].is_fault;   //小车故障状态
        bool is_loaded = carItems[vector_carid].isLoaded;
        int run_count = carItems[vector_carid].runTurn_number;
        carItem_rLock.unlock();
        if (is_fault)       //小车故障
        {
            log("---- [空车回传] 小车号: [" + std::to_string(car_id) + "] 处于故障状态, 不进行空车回传处理!");
            return;
        }
        if (originSignalCount.load() >= 1)      //头车已转两圈, 确保TCP传输空车数据有效性
        {
            if (!is_loaded)             //如果小车没有上件状态,
            {
                if (run_count <= 3)     //经过3次空车都没有扫描识别数据
                {
                    run_count += 1;
                    carItemsWriter->setRunNum(car_id, run_count);
                }
                else {  //标记小车状态为有货物,并在1号格口强制下格口
                    int port_num = test_slot_id.load();    // 设置强排口
                    int position = outports_map[port_num].position;
                    int offset = outports_map[port_num].offset;
                    bool inside = outports_map[port_num].inside;
                    log("---- [空车回传] 小车ID: [" + std::to_string(car_id) + "] 是无货状态检测到有货，强制设置格口号为: [" + std::to_string(port_num) + "]");
                    carItemsWriter->writeSlotInfo(car_id, port_num, position, offset, inside);
                    carLoop_readCarItemsVersion.fetch_add(1,std::memory_order_release);
                    // carLoop_readCarItems.store(true);       //更新了item的下件格口信息, carloop中也需要更新
                }
            }
        }
    }
    catch (const std::exception& ex) {
        log("---- [空车光电] 数据处理异常: " + std::string(ex.what()));
    }
}
void DeviceManager::slotLoop()
{
    while (m_polling)
    {
        updateSlotConfig();
        Sleep(2000);
    }
}
void DeviceManager::resetCameraPositions()
{
    auto _sqlQueryBtnClick = SqlConnectionPool::instance().acquire();
    if(!_sqlQueryBtnClick){
        log("----[sql异常] DeviceManager 连接是空指针!");
        return;
    }
    try
    {
        auto camera41Pos = _sqlQueryBtnClick->queryString("config", "name", "camera_position_one", "value");
        if (camera41Pos)
        {
            _camera41Position = std::stoi(*camera41Pos);
            log("---- [41相机] 位置重置为: " + std::to_string(_camera41Position));
        }
        auto camera42Pos = _sqlQueryBtnClick->queryString("config", "name", "camera_position_two", "value");
        if (camera42Pos)
        {
            _camera42Position = std::stoi(*camera42Pos);
            log("---- [42相机] 位置重置为: " + std::to_string(_camera42Position));
        }
    }
    catch (const std::exception& e)
    {
        log("---- [相机位置] 重置异常: " + std::string(e.what()));
    }
}
void DeviceManager::resetSlotConfigurations()
{
    try
    {
        auto _sqlQueryBtnClick = SqlConnectionPool::instance().acquire();
        if(!_sqlQueryBtnClick){
            log("----[sql异常] DeviceManager 连接是空指针!");
            return;
        }
        auto slot_config = _sqlQueryBtnClick->readTable("outport_config");
        if (!slot_config.empty())
        {
            log("---- [格口配置] 开始重置格口配置...");
            for (const auto& row : slot_config)
            {
                if (row.size() < 4)continue;
                int port_id = std::stoi(row[0]);
                int position = std::stoi(row[1]);
                int offset = std::stoi(row[2]);
                bool inside = (row[3] == "1");
                outports_map[port_id] = { port_id, position, offset, inside };
            }
        }
        auto strong_slot_config = _sqlQueryBtnClick->queryString("config", "name", "strong_slot", "value");
        if (strong_slot_config)
        {
            test_slot_id.store(std::stoi(*strong_slot_config));
            log("---- [格口配置] 强排口重置为: [" + *strong_slot_config + "]");
        }
    }
    catch (const std::exception& e)
    {
        log("---- [格口配置] 重置异常: " + std::string(e.what()));
    }
}
void DeviceManager::updateSlotConfig()  //查询数据库中格口以及强排口, 查询
{
    auto _sqlQuery = SqlConnectionPool::instance().acquire();
    if(!_sqlQuery){
        log("----[sql异常] DeviceManager 连接是空指针!");
        return;
    }
    try
    {
        auto test_slot = _sqlQuery->queryString("config", "name", "strong_slot", "value");
        if (test_slot)
        {
            int test_slotInt = std::stoi(*test_slot);
            if (test_slotInt != test_slot_id.load())
            {
                test_slot_id.store(test_slotInt);
                log("---- [强排口] 更改为:[" + *test_slot + "]");
            }
        }
        std::bitset<252> plc_slotStatus;
        if (_s7QueryPlcSlot.ReadBools274_Bitset(33, 0, 252, plc_slotStatus))
        {
            for (int vector_slot_id = 0; vector_slot_id < TotalPortNum; vector_slot_id++)
            {
                bool status = plc_slotStatus.test(vector_slot_id);
                int slot_id = vector_slot_id + 1;
                if (status != slots_status_map[slot_id])
                {
                    slots_status_map[slot_id] = status;
                    log("---- [格口状态] 格口号: [" + std::to_string(slot_id) + "] 状态变更为: [" + std::to_string(status) + "]");
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        if (IsUpLayerLine)   log("---- [up updateSlotConfig] Exception = " + std::string(e.what()));
        else log("---- [down updateSlotConfig] Exception = " + std::string(e.what()));

    }
}
void DeviceManager::carLoop()
{
    using clock = std::chrono::steady_clock;
    const auto period = std::chrono::milliseconds(12);   //10ms执行一次
    const int offsetEps = 5;       //格口偏移量误差范围,8ms
    std::vector<CarItem> copy_carItems;
    copy_carItems.resize(carItems.size());
    std::vector<CarInfo> copy_carStatus;
    copy_carStatus.resize(carStatus.size());
    std::vector<bool> copy_carLocks;
    copy_carLocks.resize(carLocks.size());
    uint64_t lastCarStatusVersion = 0;
    uint64_t lastCarItemsVersion = 0;
    int64_t prevNs = 0;
    while (m_polling)
    {
        try
        {
            auto t0 = clock::now();

            // int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t0.time_since_epoch()).count();
            // int64_t diffMs = (nowNs - prevNs) / 1'000'000;      //碰到光电到现在的时间, 也就是当前光电所在小车挡板的位置

            uint64_t ver = carLoop_readCarItemsVersion.load(std::memory_order_acquire);
            if(ver!=lastCarItemsVersion){       //版本不同,需要读取
                std::shared_lock<std::shared_mutex> read_lock(carItemsLock);
                //获取锁后再次验证版本
                uint64_t ver2 = carLoop_readCarItemsVersion.load(std::memory_order_acquire);
                if(ver2==ver){      //第一次与第二次没有变化
                    copy_carItems = carItems;
                    lastCarItemsVersion = ver;
                }else{
                    log("----[小车循环] 更新 car_item 出现了漏缺!");
                    copy_carItems = carItems;
                    lastCarItemsVersion = ver2;
                }
                read_lock.unlock();
            }
            uint64_t position_ver = carLoop_readCarStatusVersion.load(std::memory_order_acquire);
            if(position_ver!=lastCarStatusVersion){
                prevNs = lastStepTimeNs.load();    //获取最新步进时间,只有在步进触发后再读取
                if(prevNs<=0){
                    // log("----[小车循环] 最新步进时间有误! 步进时间: ["+std::to_string(prevNs)+"]");
                    continue;
                }
                std::shared_lock<std::shared_mutex> read_lock(carStatusLock);
                uint64_t position_ver2 = carLoop_readCarStatusVersion.load(std::memory_order_acquire);
                if(position_ver2 == position_ver){
                    copy_carStatus = carStatus;
                    lastCarStatusVersion = position_ver;
                }else{
                    log("----[小车循环] 更新 car_position 出现了漏缺!");
                    copy_carStatus = carStatus;
                    lastCarStatusVersion = position_ver2;
                }
                read_lock.unlock();
            }

            for (const auto& car_info : copy_carStatus)   //循环所有小车
            {
                uint64_t check_position_ver = carLoop_readCarStatusVersion.load(std::memory_order_acquire);     //循环中检查一遍
                if(check_position_ver!=lastCarStatusVersion)
                {
                    log("----[小车循环] for循环中的小车位置已被改变!跳出本次循环!");
                    break;
                }

                auto now_tp = clock::now();     //用当前时间进行计算
                auto elapsed = now_tp - t0;
                if (elapsed > period)
                {
                    log("---- [循环警告] 小车循环过长，跳出本次循环，请注意TCP命令帧发送时间!!!!");
                    break;          // 时间到，跳出本次循环, 确保小车状态最新
                }
                int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now_tp.time_since_epoch()).count();
                int64_t diffMs = (nowNs - prevNs) / 1'000'000; // ms
                if(diffMs<0) diffMs=0;


                int car_id = car_info.carID;
                int vector_carid = car_id - 1;
                if (vector_carid < 0 || vector_carid >= copy_carItems.size()){
                    continue;
                }
                const auto& car_item = copy_carItems[vector_carid];    //小车上状态及信息
                std::string code = car_item.code;
                int currentPosition = car_info.currentPosition;


                int port_num = copy_carItems[vector_carid].port_num;   //获取格口号
                if (port_num<1 || port_num>TotalPortNum) continue;      //跳过当前的
                bool slot_status = slots_status_map[port_num];   //获取格口状态

                if (car_item.isLoaded
                    && currentPosition == car_item.targetPosition
                    && std::abs(car_item.offset - diffMs) <= offsetEps
                    && originSignalCount.load() >= 1
                    && slot_status == false
                    /*&& car_lock_status == false*/
                )
                {
                    bool inside = car_item.inside;
                    handleCarUnload(car_id, inside, code, port_num, lastCarStatusVersion);          //传入lastCarStatusVersion 用于socket发送命令帧前, 判断是否为小车当前位置标志
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        catch (const std::exception& e)
        {
            log("---- [小车循环] 异常: " + std::string(e.what()));
        }
    }
}

void DeviceManager::handleCarUnload(int car_id, bool direction, std::string code, int slot_id, uint64_t lastCarStatusVersion) //下件,同时初始化小车
{
    try
    {
        if (car_id<1 || car_id>TotalCarNum) return;                        //小车号不合法
        bool ok = driveByCarID(car_id, lastCarStatusVersion, direction);   //驱动小车到对应格口
        if(ok){                                                             //下件成功
            carItemsWriter->initCarItem(car_id);                            //初始化对应小车信息
            carLoop_readCarItemsVersion.fetch_add(1, std::memory_order_release);
            log("---- [小车下件] 单号: [" + code + "], 小车号: [" + std::to_string(car_id) + "], 格口号: [" + std::to_string(slot_id) + "]");
        }
        // carLoop_readCarItems.store(true);   //更新了小车的下件格口信息, 让carloop中读取
    }
    catch (const std::exception& e)
    {
        log("---- [下件异常] 异常信息: " + std::string(e.what()));
    }
}
int DeviceManager::getTimeDiff()  //获取当前时间与上一次步进触发时间的差值, 单位为毫秒
{
    int64_t prevNs = lastStepTimeNs.load();    //上一次步进触发时间
    auto nowTp = std::chrono::steady_clock::now().time_since_epoch();   //当前时间
    int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(nowTp).count();    //转化为纳秒
    int64_t timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::nanoseconds(nowNs - prevNs)).count();     //当前走过的时间与光电触发的时间之差
    return timeDiff;
}

void DeviceManager::initCarPosition(int car_id, int current_position)
{

    if (car_id< 1 || car_id > TotalCarNum)  return;

    int vector_carid = car_id - 1;
    //只有在程序最开始起来的时候才使用
    CarInfo info;
    info.carID = car_id;            //真实小车号
    info.currentPosition = current_position;       // 初始位置可以直接设为编号（或根据实际情况调整）
    carStatus[vector_carid] = info;       // 将该小车状态存入映射，键为 carID

    // CarInfo info;
    // info.carID = car_id;
    // info.currentPosition = current_position;
    // if(car_id<=48){                     //1-48
    //     carPosition_first[vector_carid] = info;
    // }
    // else if(car_id>48&&car_id<=96){     //49-96
    //     carPosition_second[vector_carid] = info;
    // }
    // else if(car_id>96&&car_id<=144){     //97-144
    //     carPosition_thrid[vector_carid] = info;
    // }
    // else if(car_id>144&&car_id<=192){   //145-192
    //     carPosition_fourth[vector_carid] = info;
    // }else if(car_id>192&&car_id<=202){
    //     carPosition_fifth[vector_carid] = info;
    // }
}
void DeviceManager::initCarItems(int car_id)
{
    auto _sqlQuery = SqlConnectionPool::instance().acquire();
    if(!_sqlQuery){
        log("----[sql异常] DeviceManager 连接是空指针!");
        return;
    }
    try
    {
        if (car_id< 1 || car_id > TotalCarNum)  return;
        int vector_carid = car_id - 1;
        CarItem itemInfo;
        itemInfo.carID = car_id;
        itemInfo.code = "";                 //面单号为空
        itemInfo.port_num = -1;             //格口号初始化
        itemInfo.targetPosition = -1;       // 初始时无下件目标，可设为 -1 表示无目标
        itemInfo.isLoaded = false;          // 初始状态均为未装货
        itemInfo.offset = -1;               // 格口偏移量初始化
        itemInfo.inside = false;            //目标格口是否在内圈, true = 内圈, false = 外圈
        itemInfo.runTurn_number = 0;        //运行圈数, 超过两圈就强行排口
        if (originSignalCount.load() <= 1)  //只在最开始的时候进行查询判断小车是否故障
        {
            auto error_car = _sqlQuery->queryString("error_cars", "car_id", std::to_string(car_id), "answer_command");

            if (error_car&&*error_car == "error")
            {
                carLocks[vector_carid] = true;
                log("---- [Warning] CarID: [" + std::to_string(car_id) + "] is marked as faulty in the database. It will be locked.");
            }
            else
            {
                carLocks[vector_carid] = false;
            }
            itemInfo.is_fault = false;
        }
        carItems[vector_carid] = itemInfo;
    }
    catch (const std::exception& e)
    {
        log("---- [初始化] 小车信息异常: " + std::string(e.what()));
    }
}
void DeviceManager::testCamera()
{
    try
    {
        _cameraClient41.send("1");
        _cameraClient42.send("1");
    }
    catch (const std::exception& e)
    {
        log("---- [测试相机] 异常: " + std::string(e.what()));
    }
}
void DeviceManager::resetDriver()
{
    auto _sqlQueryBtnClick = SqlConnectionPool::instance().acquire();
    if(!_sqlQueryBtnClick){
        log("----[sql异常] DeviceManager 连接是空指针!");
        return;
    }
    try
    {
        auto car_speed_first = _sqlQueryBtnClick->queryString("config", "name", "car_speed_first", "value");
        if (car_speed_first) _car_speed_first.store(std::stoi(*car_speed_first));
        auto car_distance_first = _sqlQueryBtnClick->queryString("config", "name", "car_distance_first", "value");
        if (car_distance_first) _car_distance_first.store(std::stoi(*car_distance_first));
        auto car_acceleration_first = _sqlQueryBtnClick->queryString("config", "name", "car_acceleration_first", "value");
        if (car_acceleration_first) _car_acceleration_first.store(std::stoi(*car_acceleration_first));
        log("---- [驱动重置] 运行速度:[" + std::to_string(_car_speed_first.load()) + "], 运行距离:[" + std::to_string(_car_distance_first.load()) + "], 运行加速度:[" + std::to_string(_car_acceleration_first) + "]");
    }
    catch (const std::exception& e)
    {
        log("---- [重置驱动] 异常: " + std::string(e.what()));
    }
}
void DeviceManager::lockCar(int car_id)
{
    try {
        if (car_id < 1 || car_id > TotalCarNum) return;  //超出小车索引范围
        int vector_car_id = car_id - 1;
        if (!carLocks[vector_car_id])    //小车状态正常, 加锁
        {
            carLocks[vector_car_id] = true;
            log("---- [锁定小车] 设备线程中已锁住 [" + std::to_string(car_id) + "] 号小车!");
            carLoop_readCarLocks.store(true);
        }
    }
    catch (const std::exception& e)
    {
        log("---- [锁定小车] 异常: " + std::string(e.what()));
    }
}
void DeviceManager::unlockCar(int car_id)
{
    try
    {
        if (car_id < 1 || car_id > TotalCarNum) return;  //超出小车索引范围
        int vector_car_id = car_id - 1;
        if (carLocks[vector_car_id])    //小车状态锁住, 解锁
        {
            carLocks[vector_car_id] = false;
            log("---- [解锁小车] 设备线程中已解锁 [" + std::to_string(car_id) + "] 号小车!");
            carLoop_readCarLocks.store(true);
        }
    }
    catch (const std::exception& e)
    {
        log("---- [解锁小车] 异常: " + std::string(e.what()));
    }
}

