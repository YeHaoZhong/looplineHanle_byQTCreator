#ifndef DATAPROCESSMAIN_H
#define DATAPROCESSMAIN_H

#include "logger.h"
#include <QObject>
#include <QString>
#include <Qmutex>
#include <QDateTime>
#include "devicemanager.h"
#include <QThreadPool>
#include "requestapi.h"

class DataProcessMain : public QObject
{
    Q_OBJECT
public:
    explicit DataProcessMain(QObject* parent = nullptr);
    ~DataProcessMain();
    void init();
    void dbInit();
    int insertSupply_data(const std::string code, const int car_id);
    void WriteLog(const std::string& message)
    {
        Logger::getInstance().Log(message);
    }

    //ui交互
    void driveByCarid(int car_id);
    void startTestCarLoop();
    void stopTestCarLoop();
    void initCameras();
    void lockCar(int car_id);
    void unlockCar(int car_id);
    void cleanup();
private:

    DeviceManager _loopDevice;	//手摆件系统, 线体控制

    QThreadPool m_threadPool;

    RequestAPI _requestAPI;

    //程序作为客户端
    SocketConnection _cameraClient41;	//ip为 41相机
    SocketConnection _cameraClient42;	//ip为 42相机

    std::string camera41_ip = "";
    std::string camera42_ip = "";
    int _camera41_rev_port, _camera42_rev_port;
private slots:
    void onSlotReceive(const QString& code, int slot_id);
    void on41cameraDataReceived(const QByteArray& data);
    void on42cameraDataReceived(const QByteArray& data);
public slots:
    void resetCamerasPosition();
    void resetSlotConfigurations();
    void testCamera();
    void resetDriver();

};


#endif // DATAPROCESSMAIN_H
