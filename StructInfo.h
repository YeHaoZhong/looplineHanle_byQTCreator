#ifndef STRUCTINFO_H
#define STRUCTINFO_H
#include <string>
#include <vector>
struct CarInfo
{
    int carID;              //小车ID
    int currentPosition;    //当前位置
};
struct CarItem
{
    int carID;
    std::string code;       //面单信息
    //int supply_station_id;  //供包台ID
    //float weight;     //货物重量
    int port_num;           //目标格口号
    int targetPosition;     //目标格口位置
    bool isLoaded;          //true 代表有货
    int offset;             //目标格口偏移量,时间:毫秒
    bool inside;            //目标格口是否在内圈, true = 内圈, false = 外圈
    bool is_fault = false;  //小车是否故障, true = 故障
    int runTurn_number = 0; //运行圈数, 超过两圈就强行排口
};
struct OutPortInfo
{
    int port_id;    //格口编号
    int position;   //格口位置
    int offset;     //格口偏移量
    bool inside;   //true 为内圈, false 为外圈
};
struct unloadInfo
{
    int carID;
    std::string code;
    int slot;
    std::string length;
    std::string width;
    int supply_id;
};
struct LineSpeedInfo
{
    int speed;    //线体速度
    int offset;     //需要对下件处理的偏移量
    int wait_load_time;
    int one_car_time;
};
struct supplyPosition   //供包台
{
    int suuply_station_id;  //供包台ID
    std::string ip;
    int position;   //位置
    int offset;     //偏移量
    int load_offset;   //上件左右的时间偏移
};
struct CarPlcControl
{
    int index;
    std::string ip;
};
struct TerminalToSlot
{
    int terminal_code;
    //int slot_id;
    std::vector<int> slot_ids;
};
#endif // STRUCTINFO_H
