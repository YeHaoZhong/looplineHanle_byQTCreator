#include "plccontrol.h"

PlcControl::PlcControl() {


}
PlcControl::~PlcControl() {

    DisconnectFromPLC();
}

bool PlcControl::ConnectToPLC(const std::string& ip, int rack, int slot) {

    int res = client.ConnectTo(ip.c_str(), rack, slot);
    if (res == 0) {

        Logger::getInstance().Log("---- [s7 Init] S7Client connection successfully! ip: " + ip);
        return true;
    }
    else {
        Logger::getInstance().Log("---- [s7 Error] S7Client connection failed! ip: " + ip);
        return false;
    }
}
// 写入 16-bit INT
bool PlcControl::WriteInt16(int dbNumber, int startByte, int16_t value) {
    uint16_t net = htons(static_cast<uint16_t>(value)); // 转为 network order (big-endian)
    uint8_t buf[2];
    std::memcpy(buf, &net, sizeof(net));

    if (!client.Connected())   return false;
    int result = client.DBWrite(dbNumber, startByte, sizeof(buf), buf);
    if (result == 0) {
        //Logger::getInstance().Log("---- [s7 Write] S7Client write INT DB: [" + std::to_string(dbNumber) + "], addr: [" + std::to_string(startByte) + "], value: [" + std::to_string(value) + "] success! ");
        return true;
    }
    else {
        Logger::getInstance().Log("---- [Error] S7Client failed to write INT DB:" + std::to_string(dbNumber) + ", addr:" + std::to_string(startByte) + ", value:" + std::to_string(value));
        return false;
    }
}

// 写入 32-bit DINT
bool PlcControl::WriteInt32(int dbNumber, int startByte, int32_t value) {
    uint32_t net = htonl(static_cast<uint32_t>(value));
    uint8_t buf[4];
    std::memcpy(buf, &net, sizeof(net));
    int result = client.DBWrite(dbNumber, startByte, sizeof(buf), buf);
    if (result == 0) {
        Logger::getInstance().Log("S7Client write DINT DB:" + std::to_string(dbNumber) + ", addr:" + std::to_string(startByte) + ", value:" + std::to_string(value) + " success");
        return true;
    }
    else {
        Logger::getInstance().Log("S7Client failed to write DINT DB:" + std::to_string(dbNumber) + ", addr:" + std::to_string(startByte) + ", value:" + std::to_string(value));
        return false;
    }
}

// 写整个字节（覆盖）
bool PlcControl::WriteByte(int dbNumber, int startByte, uint8_t byteValue) {
    int result = client.DBWrite(dbNumber, startByte, 1, &byteValue);
    if (result == 0) {
        Logger::getInstance().Log("S7Client write BYTE DB:" + std::to_string(dbNumber) + ", addr:" + std::to_string(startByte) + ", value:" + std::to_string(byteValue) + " success");
        return true;
    }
    else {
        Logger::getInstance().Log("S7Client failed to write BYTE DB:" + std::to_string(dbNumber) + ", addr:" + std::to_string(startByte) + ", value:" + std::to_string(byteValue));
        return false;
    }
}

// 写单个位（读-改-写）
bool PlcControl::WriteDBBit(int dbNumber, int startByte, int bitIndex, bool bitValue) {
    if (bitIndex < 0 || bitIndex > 7) {
        Logger::getInstance().Log("---- [Error] WriteDBBit: invalid bitIndex " + std::to_string(bitIndex));
        return false;
    }

    uint8_t byteBuf = 0;
    int resRead = client.DBRead(dbNumber, startByte, 1, &byteBuf);
    if (resRead != 0) {
        Logger::getInstance().Log("---- [Error] S7Client failed to read byte before write bit - DB:" + std::to_string(dbNumber) + ", addr:" + std::to_string(startByte));
        return false;
    }

    uint8_t old = byteBuf;
    if (bitValue) {
        byteBuf |= static_cast<uint8_t>(1u << bitIndex);
    }
    else {
        byteBuf &= static_cast<uint8_t>(~(1u << bitIndex));
    }

    if (byteBuf == old) {
        // 值没有变化，也可以视为成功，或者仍然写回以触发 PLC 内部事件
        //Logger::getInstance().Log("WriteDBBit: bit unchanged DB:" + std::to_string(dbNumber) + ", addr:" + std::to_string(startByte) + ", bit:" + std::to_string(bitIndex));
        // 可以直接返回 true，也可以仍然写一次：
        return true;
    }

    int resWrite = client.DBWrite(dbNumber, startByte, 1, &byteBuf);
    if (resWrite == 0) {
        //Logger::getInstance().Log(std::string("S7Client write DB bit success - DB:") + std::to_string(dbNumber) + ", addr:" + std::to_string(startByte) + ", bit:" + std::to_string(bitIndex) + ", value:" + (bitValue ? "true" : "false"));
        return true;
    }
    else {
        Logger::getInstance().Log(std::string("---- [Error] S7Client failed to write DB bit - DB:") + std::to_string(dbNumber) + ", addr:" + std::to_string(startByte) + ", bit:" + std::to_string(bitIndex));
        return false;
    }
}


template void PlcControl::WriteDB<int>(int dbNumber, int startByte, const int& value);

void PlcControl::DisconnectFromPLC() {

    client.Disconnect();
}
