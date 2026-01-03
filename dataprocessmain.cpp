#include "dataprocessmain.h"
#include <QMetaObject>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QTcpSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QtConcurrent/QtConcurrent>
#include <sstream>
#include "sqlconnectionpool.h"
extern std::tuple<std::string, std::string, int, int> splitUdpMessage(const std::string& msg, int num);
extern std::string currentDateTimeString();
extern std::string getCurrentTime();
extern QByteArray hmacSha256(const QByteArray& key, const QByteArray& message);
extern QByteArray hmacSha256Raw(const QByteArray& key, const QByteArray& message);
DataProcessMain::DataProcessMain(QObject* parent)
    : QObject(parent)
{
    //整个进程只调用一次 WSAStartup
#ifdef _WIN32
    WSADATA wsaData;
    int wsaRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaRet != 0) {
        WriteLog("---- [Error] WSAStartup failed: " + std::to_string(wsaRet));
        // 可视情况决定是否返回/退出
    }
#endif
    _camera41_rev_port = 2015;
    _camera42_rev_port = 2012;
}
DataProcessMain::~DataProcessMain()
{

}
void DataProcessMain::init()
{
    try
    {
        m_threadPool.setMaxThreadCount(20);
        connect(&_requestAPI, &RequestAPI::getSlotSuccess, this, &DataProcessMain::onSlotReceive);	//将请求返回结果 连接到 onSlotReceive
        dbInit();
        initCameras();
        _loopDevice.init();
    }
    catch (const std::exception& e)
    {
        WriteLog("---- [dataProcess] Exception : " + std::string(e.what()));
    }
}
void DataProcessMain::cleanup()
{
    try
    {
        _cameraClient41.disconnect();
        _cameraClient42.disconnect();
        _loopDevice.cleanup();
        WSACleanup();
    }
    catch (const std::exception& e)
    {
        WriteLog("---- [dataProcess Destructor] Exception : " + std::string(e.what()));
    }
}
void DataProcessMain::initCameras()
{
    try
    {
        if (camera41_ip == "" || camera42_ip == "" || _camera41_rev_port == 0 || _camera42_rev_port == 0)
        {
            WriteLog("---- [相机初始化] 配置参数异常, 按照默认设置配置");
            camera41_ip = "192.168.93.41";
            camera42_ip = "192.168.93.42";
            _camera41_rev_port = 2015;
            _camera42_rev_port = 2012;
        }

        if (_cameraClient41.connectTo(camera41_ip, _camera41_rev_port))	WriteLog("---- [41相机] 接收端口连接成功!");
        else    WriteLog("---- [41相机] 接收端口连接失败!");
        Sleep(20);
        if (_cameraClient42.connectTo(camera42_ip, _camera42_rev_port))	WriteLog("---- [42相机] 接收端口连接成功!");
        else    WriteLog("---- [42相机] 接收端口连接失败!");
        Sleep(20);
        connect(&_cameraClient41.client, &SocketClient::dataReceived, this, &DataProcessMain::on41cameraDataReceived);
        connect(&_cameraClient42.client, &SocketClient::dataReceived, this, &DataProcessMain::on42cameraDataReceived);
    }
    catch (const std::exception& e)
    {
        WriteLog("---- [相机初始化] 异常 : " + std::string(e.what()));
    }
}
void DataProcessMain::driveByCarid(int car_id)
{
    try
    {
        _loopDevice.driveByCarID(car_id, car_id-1);
    }
    catch (const std::exception& e)
    {
        WriteLog("---- [driveByCarid] Exception : " + std::string(e.what()));
    }
}
void DataProcessMain::on41cameraDataReceived(const QByteArray& data)	//(SF6093319807519)
{
    try
    {
        _camera41Count += 1;
        std::string dataStr = data.toStdString();
        std::stringstream ss(dataStr);
        std::string item;
        std::vector<std::string> parts;
        std::string code = "";
        while (std::getline(ss, item, ','))
        {
            parts.push_back(item);
            //WriteLog("---- test  split: " + item);
        }
        if (parts.size() >= 2)
        {
            code = parts[1];
        }
        if (code == "NoRead" || code == "")
        {
            //WriteLog("---- [41相机回传] 无效数据:[" + code + "]");
            return;
        }

        auto [dev_camera41Count,car_id]  = _loopDevice.carToCamera41();
        if(dev_camera41Count!=_camera41Count){                          //这次计数与光电触发的计数不一致,忽略这次的单号回传!会绑错小车号!
            _loopDevice.updateCamera41Count(_camera41Count);
            return;
        }
        WriteLog("---- [41相机回传] 面单号:[" + code + "], 小车号: [" + std::to_string(car_id) + "]");
        int slot_id = insertSupply_data(code, car_id);	//判断单号是否插入过数据库或请求过
        if (slot_id == -1)	//未插入过数据库, 未进行请求
        {
            WriteLog("---- [物件数据] 单号:[" + code + "] 入库, 请求格口号.");
            QMetaObject::invokeMethod(&_requestAPI,
                                      "requestForSlot",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, QString::fromStdString(code)));
            _loopDevice.updateCodeToCarMap(code, car_id);	//更新面单对应的小车
        }
        else if (slot_id == -10)	//已插入过数据库, 未请求到格口
        {
            QMetaObject::invokeMethod(&_requestAPI,
                                      "requestForSlot",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, QString::fromStdString(code)));
            _loopDevice.updateCodeToCarMap(code, car_id);
        }
        else
        {
            WriteLog("---- [物件数据] 单号:[" + code + "] 已请求, 格口号:[" + std::to_string(slot_id) + "], 更新小车列表..");
            _loopDevice.updateCodeToCarMap(code, car_id);	//更新面单对应的小车
            _loopDevice.updateSlotByCarID(car_id, slot_id);
        }

    }
    catch (const std::exception& ex)
    {
        WriteLog(std::string("---- [41相机回传] 处理异常: ") + ex.what());
    }
}
// void DataProcessMain::on41cameraDataReceived(const QByteArray& data)	//,434965221112892,041,
// {
//     try
//     {
//         std::string dataStr = data.toStdString();
//         std::stringstream ss(dataStr);
//         std::string item;
//         std::vector<std::string> parts;
//         std::string code = "";
//         std::string carid_str = "";
//         while (std::getline(ss, item, ','))
//         {
//             parts.push_back(item);
//             //WriteLog("---- test  split: " + item);
//         }
//         if (parts.size() >= 3)
//         {
//             code = parts[1];
//             carid_str = parts[2];
//         }
//         if (code == "NoRead" || code == "")
//         {
//             //WriteLog("---- [41相机回传] 无效数据:[" + code + "]");
//             return;
//         }
//         int car_id = std::stoi(carid_str);
//         if(car_id == 999) return;
//         WriteLog("---- [41相机回传] 面单号:[" + code + "], 小车号: [" + std::to_string(car_id) + "]");
//         int slot_id = insertSupply_data(code, car_id);	//判断单号是否插入过数据库或请求过
//         if (slot_id == -1)	//未插入过数据库, 未进行请求
//         {
//             WriteLog("---- [物件数据] 单号:[" + code + "] 入库, 请求格口号.");
//             QMetaObject::invokeMethod(&_requestAPI,
//                                       "requestForSlot",
//                                       Qt::QueuedConnection,
//                                       Q_ARG(QString, QString::fromStdString(code)));
//             _loopDevice.updateCodeToCarMap(code, car_id);	//更新面单对应的小车
//         }
//         else if (slot_id == -10)	//已插入过数据库, 未请求到格口
//         {
//             QMetaObject::invokeMethod(&_requestAPI,
//                                       "requestForSlot",
//                                       Qt::QueuedConnection,
//                                       Q_ARG(QString, QString::fromStdString(code)));
//             _loopDevice.updateCodeToCarMap(code, car_id);
//         }
//         else
//         {
//             WriteLog("---- [物件数据] 单号:[" + code + "] 已请求, 格口号:[" + std::to_string(slot_id) + "], 更新小车列表..");
//             _loopDevice.updateCodeToCarMap(code, car_id);	//更新面单对应的小车
//             _loopDevice.updateSlotByCarID(car_id, slot_id);
//         }

//     }
//     catch (const std::exception& ex)
//     {
//         WriteLog(std::string("---- [41相机回传] 处理异常: ") + ex.what());
//     }
// }
void DataProcessMain::on42cameraDataReceived(const QByteArray& data)
{
    try
    {
        _camera42Count += 1;                                            //先加1, 设备中的光电触发也先加1
        std::string dataStr = data.toStdString();
        if (dataStr == "NoRead")
        {
            //WriteLog("---- [42相机回传] 无效数据:[" + dataStr + "]");
            return;
        }
        auto [dev_camera42Count,car_id]  = _loopDevice.carToCamera42();
        if(dev_camera42Count!=_camera42Count){                          //这次计数与光电触发的计数不一致,忽略这次的单号回传!会绑错小车号!
            WriteLog("---- [42相机回传] 计数不一致! 忽略本次回传!");
            _loopDevice.updateCamera42Count(_camera42Count);
            return;
        }

        WriteLog("---- [42相机回传] 面单号:[" + dataStr + "], 小车号: [" + std::to_string(car_id) + "]");
        int slot_id = insertSupply_data(dataStr, car_id);	//判断单号是否插入过数据库或请求过
        if (slot_id == -1)	//未插入过数据库, 未进行请求
        {
            WriteLog("---- [物件数据] 单号:[" + dataStr + "] 入库, 请求格口号.");
            QMetaObject::invokeMethod(&_requestAPI,
                                      "requestForSlot",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, QString::fromStdString(dataStr)));
            _loopDevice.updateCodeToCarMap(dataStr, car_id);	//更新面单对应的小车
        }
        else if (slot_id == -10)	//已插入过数据库, 未请求到格口
        {
            QMetaObject::invokeMethod(&_requestAPI,
                                      "requestForSlot",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, QString::fromStdString(dataStr)));
            _loopDevice.updateCodeToCarMap(dataStr, car_id);
        }
        else
        {
            WriteLog("---- [物件数据] 单号:[" + dataStr + "] 已请求, 格口号:[" + std::to_string(slot_id) + "], 更新小车列表..");
            _loopDevice.updateCodeToCarMap(dataStr, car_id);	//更新面单对应的小车
            _loopDevice.updateSlotByCarID(car_id, slot_id);
        }
    }
    catch (const std::exception& ex)
    {
        WriteLog(std::string("---- [42相机回传] 处理异常: ") + ex.what());
    }
}
void DataProcessMain::onSlotReceive(const QString& code, int slot_id)
{
    try
    {
        int copy_slot_id = slot_id;
        std::string copy_code = code.toStdString();
        WriteLog("---- [请求回传] 单号:[" + copy_code + "], 格口号:[" + std::to_string(copy_slot_id) + "]");
        _loopDevice.updateSlotByCode(copy_code, copy_slot_id);
        auto _sqlQuery = SqlConnectionPool::instance().acquire();
        if(!_sqlQuery){
            Logger::getInstance().Log("----[sql异常] 连接空指针!");
            return;
        }
        _sqlQuery->updateValue("supply_data", "code", copy_code, "slot_id", std::to_string(slot_id));
    }
    catch (const std::exception& ex)
    {
        WriteLog(std::string("---- [请求回传] 异常: ") + ex.what());
    }
}
void DataProcessMain::dbInit()
{
    try
    {
        auto _sqlQuery = SqlConnectionPool::instance().acquire();
        if(!_sqlQuery){
            Logger::getInstance().Log("----[sql异常] 连接空指针!");
            return;
        }
        auto db_camera41_ip = _sqlQuery->queryString("config", "name", "camera_ip_one", "value");
        if (db_camera41_ip)	camera41_ip = *db_camera41_ip;

        auto db_camera42_ip = _sqlQuery->queryString("config", "name", "camera_ip_two", "value");
        if (db_camera42_ip)	camera42_ip = *db_camera42_ip;

        auto db_camera41_port = _sqlQuery->queryString("config", "name", "camera_receive_one", "value");
        if (db_camera41_port)	_camera41_rev_port = std::stoi(*db_camera41_port);
        else
        {
            _camera41_rev_port = 0;
        }

        auto db_camera42_port = _sqlQuery->queryString("config", "name", "camera_receive_two", "value");
        if (db_camera42_port)	_camera42_rev_port = std::stoi(*db_camera42_port);
        else
        {
            _camera42_rev_port = 0;
        }
    }
    catch (const std::exception& e)
    {
        WriteLog("---- [dbInit] Exception : " + std::string(e.what()));
    }
}
int DataProcessMain::insertSupply_data(const std::string code, const int car_id)
{
    try
    {
        auto _sqlQuery = SqlConnectionPool::instance().acquire();
        if(!_sqlQuery){
            Logger::getInstance().Log("----[sql异常] 连接空指针!");
            return -1;
        }
        std::string weight = "1";
        auto check = _sqlQuery->queryString("supply_data", "code", code, "slot_id");
        std::string now_time = getCurrentTime();
        if (!check || !check.has_value())		//判断是否已入库, 未入库返回 -1
        {
            const std::vector<std::string> column = { "code","weight","scan_time","car_id","slot_id" };
            const std::vector<std::string> values = { code,weight,now_time, std::to_string(car_id),"-1" };
            _sqlQuery->insertRow("supply_data", column, values);
            return -1;
        }
        else	//单号已入库
        {
            const std::vector<std::string> columns = { "code","weight","scan_time","car_id","slot_id"};
            int slot_id = std::stoi(*check);
            if (slot_id == -1)		//已入库, 但还未请求到格口号, 返回-10
            {
                const std::vector<std::string> values = { code, weight, now_time, std::to_string(car_id),"-1" };
                _sqlQuery->updateRow("supply_data", columns, values, "code", code, false);
                return -10;
            }
            const std::vector<std::string> values = { code, weight, now_time, std::to_string(car_id),std::to_string(slot_id) };	//更新数据信息, 保持原来的格口号
            _sqlQuery->updateRow("supply_data", columns, values, "code", code, false);
            return slot_id;
        }
    }
    catch (const std::exception& e)
    {
        WriteLog("---- [insert supplyData] Exception : " + std::string(e.what()));
    }
    return -1;
}
void DataProcessMain::startTestCarLoop()
{
    try
    {
        _loopDevice.startCarTestLoop();
    }
    catch (const std::exception& e)
    {
        WriteLog("---- [startTestCarLoop] Exception : " + std::string(e.what()));
    }
}
void DataProcessMain::stopTestCarLoop()
{
    try
    {
        _loopDevice.stopCarTestLoop();
    }
    catch (const std::exception& e)
    {
        WriteLog("---- [stopTestCarLoop] Exception : " + std::string(e.what()));
    }
}
void DataProcessMain::resetCamerasPosition()
{
    _loopDevice.resetCameraPositions();
}
void DataProcessMain::resetSlotConfigurations()
{
    _loopDevice.resetSlotConfigurations();
}
void DataProcessMain::testCamera()
{
    _loopDevice.testCamera();
}
void DataProcessMain::resetDriver()
{
    _loopDevice.resetDriver();
}
void DataProcessMain::lockCar(int car_id)
{
    try
    {
        auto _sqlQuery = SqlConnectionPool::instance().acquire();
        if(!_sqlQuery){
            Logger::getInstance().Log("----[sql异常] 连接空指针!");
            return;
        }
        if (car_id < 1 || car_id >= 203) return;
        auto info = _sqlQuery->queryString("error_cars", "car_id", std::to_string(car_id), "answer_command");
        if (info)		//没被记录数据库中
        {
            if (*info != "error")
            {
                bool ok = _sqlQuery->updateValue("error_cars", "car_id", std::to_string(car_id), "answer_command", "error");
                if (ok) WriteLog("---- [小车锁定] [" + std::to_string(car_id) + "] 上锁成功!");
            }
            else   WriteLog("---- [小车锁定] [" + std::to_string(car_id) + "] 是上锁状态, 无需再上锁.");
        }
        _loopDevice.lockCar(car_id);
    }
    catch (const std::exception& e)
    {
        WriteLog("---- [小车锁定] 异常: " + std::string(e.what()));
    }

}
void DataProcessMain::unlockCar(int car_id)
{
    try{
        auto _sqlQuery = SqlConnectionPool::instance().acquire();
        if(!_sqlQuery){
            Logger::getInstance().Log("----[sql异常] 连接空指针!");
            return;
        }
        if (car_id < 1 || car_id >= 203) return;
        auto info = _sqlQuery->queryString("error_cars", "car_id", std::to_string(car_id), "answer_command");
        if (info)		//记录数据库中
        {
            if (*info != "-1") {
                bool ok = _sqlQuery->updateValue("error_cars", "car_id", std::to_string(car_id), "answer_command", "-1");
                if (ok) WriteLog("---- [小车解锁] [" + std::to_string(car_id) + "] 解锁成功!");
            }
            else  WriteLog("---- [小车解锁] [" + std::to_string(car_id) + "] 是解锁状态, 无需再解锁.");
        }
        _loopDevice.unlockCar(car_id);
    }
    catch (const std::exception& e)
    {
        WriteLog("---- [小车解锁] 异常: " + std::string(e.what()));
    }
}
