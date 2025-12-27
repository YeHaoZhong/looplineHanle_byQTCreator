#include "caritemswritethread.h"
#include <algorithm>
#include <chrono>

CarItemsWriteThread::CarItemsWriteThread(std::vector<CarItem>& carItemsRef, std::shared_mutex& carItemsLockRef)
    : carItems_(carItemsRef), carItemsLock_(carItemsLockRef)
{
    stopping_.store(false); // 确保初始为 false
}
void CarItemsWriteThread::startLoop()
{
    if (worker_.joinable()) {
        return;
    }
    stopping_.store(false); // 再次确保 false（防止之前曾 stop 过）
    worker_ = std::thread([this]() {
        try {
            this->workerLoop();
        }
        catch (const std::exception& e) {
            log(std::string("---- [writeThread] workerLoop exception: ") + e.what());
        }
        catch (...) {
            Logger::getInstance().Log("---- [writeThread] workerLoop unknown exception");
        }
    });
    // 立刻记录线程是否可 join（表示已启动）
    if (worker_.joinable()) {
        log("---- [writeThread] Start write loop!");
    }
    else {
        log("---- [writeThread] worker not joinable after start");
    }
}

CarItemsWriteThread::~CarItemsWriteThread()
{
    stopping_.store(true);
    qCv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

int CarItemsWriteThread::indexForCarID_nocheck(int carID)
{
    int idx = carID - 1;
    if (idx >= 0 && idx < (int)carItems_.size() && carItems_[idx].carID == carID) {
        return idx;
    }
    for (int i = 0; i < (int)carItems_.size(); ++i) {
        if (carItems_[i].carID == carID) return i;
    }
    return -1;
}
void CarItemsWriteThread::writeSlotInfo(int carID, int port_num, int position, int offset, bool inside)
{
    // 尝试立即同步写（非阻塞）
    std::unique_lock<std::shared_mutex> wlock(carItemsLock_, std::try_to_lock);
    if (wlock.owns_lock()) {
        int idx = indexForCarID_nocheck(carID);
        if (idx >= 0) {
            carItems_[idx].port_num = port_num;
            carItems_[idx].targetPosition = position;
            carItems_[idx].offset = offset;
            carItems_[idx].inside = inside;
            carItems_[idx].isLoaded = true;
            log("---- [write slotInfo] Sync car_id: [" + std::to_string(carID) + "], slot_id: [" + std::to_string(port_num) + "], position: [" + std::to_string(position) + "]");
        }
        return;
    }
    {
        std::lock_guard<std::mutex> lk(qMutex_);
        q_.push(Event::MakeWriteSlot(carID, port_num, position, offset, inside));
    }
    qCv_.notify_one();
}

//void CarItemsWriteThread::writeItemInfo(int carID, std::string code)    //上件，转圈数为0
//{
//    std::unique_lock<std::shared_mutex> wlock(carItemsLock_, std::try_to_lock);
//    if (wlock.owns_lock()) {
//        int idx = indexForCarID_nocheck(carID);
//        if (idx >= 0) {
//            carItems_[idx].code = code;
//            carItems_[idx].runTurn_number = 0;
//            carItems_[idx].isLoaded = true;
//        }
//        return;
//    }
//    {
//        std::lock_guard<std::mutex> lk(qMutex_);
//        q_.push(Event::MakeWriteItem(carID, code));
//    }
//    qCv_.notify_one();
//}
void CarItemsWriteThread::writeItemInfo(int carID, std::string code)
{
    std::unique_lock<std::shared_mutex> wlock(carItemsLock_, std::try_to_lock);
    if (wlock.owns_lock()) {
        int idx = indexForCarID_nocheck(carID);
        if (idx >= 0) {
            carItems_[idx].code = code;
            carItems_[idx].runTurn_number = 0;
            if (!carItems_[idx].code.empty()) carItems_[idx].isLoaded = true;
            log("---- [writeItem] sync car=" + std::to_string(carID) + " idx=" + std::to_string(idx) + " code=" + carItems_[idx].code);
        }
        return;
    }
    {
        std::lock_guard<std::mutex> lk(qMutex_);
        auto ev = Event::MakeWriteItem(carID, std::move(code)); // ensure move
        q_.push(std::move(ev));
        log("---- [queue] queued WriteItem car=" + std::to_string(carID) + " qsize=" + std::to_string(q_.size()));
    }
    qCv_.notify_one();
}

void CarItemsWriteThread::setRunNum(int carID, int runTurn_number)
{
    std::unique_lock<std::shared_mutex> wlock(carItemsLock_, std::try_to_lock);
    if (wlock.owns_lock()) {
        int idx = indexForCarID_nocheck(carID);
        if (idx >= 0) carItems_[idx].runTurn_number = runTurn_number;
        return;
    }
    {
        std::lock_guard<std::mutex> lk(qMutex_);
        q_.push(Event::MakeSetRunNum(carID, runTurn_number));
    }
    qCv_.notify_one();
}

void CarItemsWriteThread::initCarItem(int carID)
{
    std::unique_lock<std::shared_mutex> wlock(carItemsLock_, std::try_to_lock);
    if (wlock.owns_lock())
    {
        int idx = indexForCarID_nocheck(carID);
        if (idx >= 0) {
            //carItems_[idx].code = "";                 //面单号为空
            carItems_[idx].port_num = -1;             //格口号初始化
            carItems_[idx].targetPosition = -1;       // 初始时无下件目标，可设为 -1 表示无目标
            carItems_[idx].isLoaded = false;          // 初始状态均为未装货
            carItems_[idx].offset = -1;               // 格口偏移量初始化
            carItems_[idx].inside = false;            //目标格口是否在内圈, true = 内圈, false = 外圈
            carItems_[idx].runTurn_number = 0;        //运行圈数, 超过两圈就强行排口
        }
        return;
    }
    {
        std::lock_guard<std::mutex> lk(qMutex_);
        q_.push(Event::MakeInitItem(carID));
    }
    qCv_.notify_one();
}
void CarItemsWriteThread::workerLoop()
{
    while (true) {
        log("---- [writeThread] in write thread!");
        std::queue<Event> local_q;
        {
            std::unique_lock<std::mutex> lk(qMutex_);
            qCv_.wait(lk, [this] { return stopping_.load() || !q_.empty(); });
            if (stopping_.load() && q_.empty()) break;
            std::swap(local_q, q_);
        }

        // 批量处理事件时持有写锁以减少频繁上锁/解锁
        std::unique_lock<std::shared_mutex> wlock(carItemsLock_);
        while (!local_q.empty()) {
            Event ev = std::move(local_q.front());
            local_q.pop();

            int idx = indexForCarID_nocheck(ev.carID);
            if (ev.type == EventType::WriteSlot) {
                if (idx >= 0) {
                    carItems_[idx].port_num = ev.port_num;
                    carItems_[idx].targetPosition = ev.position;
                    carItems_[idx].offset = ev.offset;
                    carItems_[idx].inside = ev.inside;
                    carItems_[idx].isLoaded = true;
                    log("---- [write slotInfo] Wait car_id: [" + std::to_string(ev.carID) + "], slot_id: [" + std::to_string(ev.port_num) + "], position: [" + std::to_string(ev.position) + "]");
                }
            }
            else if (ev.type == EventType::WriteItem) {
                if (idx >= 0) {
                    carItems_[idx].code = ev.code;
                    carItems_[idx].runTurn_number = ev.runTurn_number;
                    carItems_[idx].isLoaded = true;
                    log("---- [write itemInfo] Wait car_id: [" + std::to_string(ev.carID) + "], code: [" + ev.code + "]");
                }
            }
            else if (ev.type == EventType::SetRunNum) {
                if (idx >= 0) {
                    carItems_[idx].runTurn_number = ev.runTurn_number;
                }
            }
            else if (ev.type == EventType::InitItem) {
                if (idx >= 0) {
                    carItems_[idx].port_num = -1;             //格口号初始化
                    carItems_[idx].targetPosition = -1;       // 初始时无下件目标，可设为 -1 表示无目标
                    carItems_[idx].isLoaded = false;          // 初始状态均为未装货
                    carItems_[idx].offset = -1;               // 格口偏移量初始化
                    carItems_[idx].inside = false;            //目标格口是否在内圈, true = 内圈, false = 外圈
                    carItems_[idx].runTurn_number = 0;        //运行圈数, 超过两圈就强行排口
                    log("---- [write initItem] Wait car_id: [" + std::to_string(ev.carID) + "]");
                }
            }
        }
    }
    log("---- [writeThread] Write loop exit!");
}

void CarItemsWriteThread::waitUntilIdle()
{
    while (true) {
        {
            std::lock_guard<std::mutex> lk(qMutex_);
            if (q_.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
