#ifndef CARITEMSWRITETHREAD_H
#define CARITEMSWRITETHREAD_H
#include "StructInfo.h"   // 你的 CarItem 定义所在头
#include <vector>
#include <thread>
#include <shared_mutex>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include "Logger.h"

class CarItemsWriteThread {
public:
    CarItemsWriteThread(std::vector<CarItem>& carItemsRef, std::shared_mutex& carItemsLockRef);
    ~CarItemsWriteThread();

    CarItemsWriteThread(const CarItemsWriteThread&) = delete;
    CarItemsWriteThread& operator=(const CarItemsWriteThread&) = delete;

    // 三个写接口
    void writeSlotInfo(int carID, int port_num, int position, int offset, bool inside);
    void writeItemInfo(int carID, std::string code);
    void setRunNum(int carID, int runTurn_number);
    void initCarItem(int carID);
    void waitUntilIdle(); // 可选；用于测试/优雅停机
    void log(const std::string& msg)
    {
        Logger::getInstance().Log(msg);
    }
    void startLoop();
private:
    // 事件类型与结构体（无需 std::function）
    enum class EventType : uint8_t {
        WriteSlot,
        WriteItem,
        SetRunNum,
        InitItem
    };
    struct Event {
        EventType type;
        int carID;
        int port_num = -1;
        int position = -1;
        int offset = -1;
        bool inside = false;
        bool isLoaded = false;
        std::string code;
        int runTurn_number = 0;

        // 构造器辅助
        static Event MakeWriteSlot(int carID_, int port_num_, int position_, int offset_, bool inside_) {
            Event e;
            e.type = EventType::WriteSlot;
            e.port_num = port_num_;
            e.position = position_;
            e.offset = offset_;
            e.inside = inside_;
            e.isLoaded = true;
            return e;
        }
        static Event MakeWriteItem(int carID_, std::string code_) {
            Event e;
            e.type = EventType::WriteItem;
            e.code = std::move(code_);
            e.runTurn_number = 0;
            e.isLoaded = true;
            return e;
        }
        static Event MakeSetRunNum(int carID_, int runTurn_number_) {
            Event e;
            e.type = EventType::SetRunNum;
            e.runTurn_number = runTurn_number_;
            return e;
        }
        static Event MakeInitItem(int carID_) {
            Event e;
            e.type = EventType::InitItem;
            //e.code = "";                 //面单号为空
            e.port_num = -1;             //格口号初始化
            e.position = -1;       // 初始时无下件目标，可设为 -1 表示无目标
            e.isLoaded = false;          // 初始状态均为未装货
            e.offset = -1;               // 格口偏移量初始化
            e.runTurn_number = 0;        //运行圈数, 超过两圈就强行排口
            return e;
        }
    };

private:
    std::vector<CarItem>& carItems_;
    std::shared_mutex& carItemsLock_;

    std::mutex qMutex_;
    std::condition_variable qCv_;
    std::queue<Event> q_;
    std::atomic<bool> stopping_{ false };
    std::thread worker_;

    void workerLoop();
    int indexForCarID_nocheck(int carID);
};



#endif // CARITEMSWRITETHREAD_H
