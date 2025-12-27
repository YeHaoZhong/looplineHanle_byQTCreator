#ifndef PLCCONTROL_H
#define PLCCONTROL_H

#include "snap7.h"
#include <string>
#include "Logger.h"
#include <vector>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <array>
#include <winsock2.h> // 包含字节序转换函数
#include <bitset>

struct PlcInts274 {
    // S7 INT 是 2 字节、有符号 -> 使用 int16_t
    std::array<int16_t, 252> values;
};

class PlcControl {
public:
    PlcControl();
    ~PlcControl(); // 析构函数，释放资源

    bool ConnectToPLC(const std::string& ip, int rack = 0, int slot = 1);
    // 写入 16-bit 有符号 INT（S7 INT）
    bool WriteInt16(int dbNumber, int startByte, int16_t value);

    // 写入 32-bit 有符号 DINT（S7 DINT）
    bool WriteInt32(int dbNumber, int startByte, int32_t value);

    // 写单个位（DB byte 的 bitIndex: 0..7，0 = LSB）
    bool WriteDBBit(int dbNumber, int startByte, int bitIndex, bool bitValue);

    // 可选：写一个字节（直接覆盖该字节）
    bool WriteByte(int dbNumber, int startByte, uint8_t byteValue);
    template <typename T>
    void WriteDB(int dbNumber, int startByte, const T& value) {
        static_assert(std::is_integral<T>::value, "T must be an integral type");

        T networkValue = toNetworkByteOrder(value);
        uint8_t buffer[sizeof(T)];
        std::memcpy(buffer, &networkValue, sizeof(T));

        int result = client.DBWrite(dbNumber, startByte, sizeof(T), buffer);
        if (result == 0) {
            Logger::getInstance().Log("S7Client write DB:" + std::to_string(dbNumber) + ", address:" + std::to_string(startByte) + ", value:" + std::to_string(value) + " successfully!");
        }
        else {
            Logger::getInstance().Log("S7Client failed to write DB:" + std::to_string(dbNumber) + ", address:" + std::to_string(startByte) + ", value:" + std::to_string(value));
        }
    }

    void DisconnectFromPLC();

    // -------------- 新增：读取整型（1/2/4 字节） --------------
    // 返回 true 表示读取成功并将结果写入 outValue；false 表示失败
    template <typename T>
    bool ReadDB(int dbNumber, int startByte, T& outValue) {
        static_assert(std::is_integral<T>::value, "T must be an integral type");

        uint8_t buffer[sizeof(T)] = { 0 };
        int result = client.DBRead(dbNumber, startByte, sizeof(T), buffer);
        if (result != 0) {
            Logger::getInstance().Log("S7Client failed to read DB:" + std::to_string(dbNumber) + ", address:" + std::to_string(startByte));
            return false;
        }

        if constexpr (sizeof(T) == 1) {
            // 单字节直接取值
            outValue = static_cast<T>(buffer[0]);
        }
        else if constexpr (sizeof(T) == 2) {
            uint16_t net = 0;
            std::memcpy(&net, buffer, sizeof(net));
            uint16_t host = ntohs(net);
            using U = std::make_unsigned_t<T>;
            outValue = static_cast<T>(static_cast<U>(host));
        }
        else if constexpr (sizeof(T) == 4) {
            uint32_t net = 0;
            std::memcpy(&net, buffer, sizeof(net));
            uint32_t host = ntohl(net);
            using U = std::make_unsigned_t<T>;
            outValue = static_cast<T>(static_cast<U>(host));
        }
        else {
            static_assert(sizeof(T) <= 4, "Unsupported integral type size");
        }

        Logger::getInstance().Log("S7Client read DB:" + std::to_string(dbNumber) + ", address:" + std::to_string(startByte) + ", value:" + std::to_string((long long)outValue) + " successfully!");
        return true;
    }

    // -------------- 新增：读取 DB 中的单个位（bool） --------------
    // bitIndex: 0..7 (0 = LSB)
    bool ReadDBBit(int dbNumber, int startByte, int bitIndex, bool& outValue) {
        if (bitIndex < 0 || bitIndex > 7) {
            Logger::getInstance().Log("ReadDBBit: invalid bitIndex " + std::to_string(bitIndex));
            return false;
        }

        uint8_t buffer = 0;
        int result = client.DBRead(dbNumber, startByte, 1, &buffer);
        if (result != 0) {
            Logger::getInstance().Log("S7Client failed to read DB bit - DB:" + std::to_string(dbNumber) + ", byte:" + std::to_string(startByte) + ", bit:" + std::to_string(bitIndex));
            return false;
        }

        outValue = ((buffer >> bitIndex) & 0x1) != 0;
        Logger::getInstance().Log("S7Client read DB bit - DB:" + std::to_string(dbNumber) + ", byte:" + std::to_string(startByte) + ", bit:" + std::to_string(bitIndex) + ", value:" + std::string(outValue ? "true" : "false"));
        return true;
    }

    bool ReadInts274(int dbNumber, int startByte, int count, PlcInts274& out)
    {
        constexpr size_t BYTES_PER = 2; // INT = 2 bytes
        const size_t totalBytes = count * BYTES_PER;
        std::vector<uint8_t> buffer(totalBytes);
        int result = client.DBRead(dbNumber, startByte, static_cast<int>(totalBytes), buffer.data());
        if (result != 0) {
            Logger::getInstance().Log("S7Client failed to bulk read DB:" + std::to_string(dbNumber) + ", addr:" + std::to_string(startByte));
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            uint16_t net = 0;
            // 拷贝两字节到 net（按前面 ReadDB 的做法）
            std::memcpy(&net, &buffer[i * BYTES_PER], BYTES_PER);
            uint16_t host = ntohs(net);                // network (big-endian) -> host
            out.values[i] = static_cast<int16_t>(host); // S7 INT 是有符号
        }

        //Logger::getInstance().Log("S7Client bulk read " + std::to_string(count) + " INTs from DB:" + std::to_string(dbNumber));
        return true;
    }

    bool ReadBools274_Bitset(int dbNumber, int startByte, int count, std::bitset<252>& outBits)
    {
        size_t COUNT = count;
        size_t BYTES = (COUNT + 7) / 8;
        std::vector<uint8_t> buffer(BYTES);

        int result = client.DBRead(dbNumber, startByte, static_cast<int>(BYTES), buffer.data());
        if (result != 0) return false;

        for (size_t i = 0; i < COUNT; ++i) {
            size_t byteIdx = i / 8;
            int bitIdx = static_cast<int>(i % 8); // LSB=0
            bool b = ((buffer[byteIdx] >> bitIdx) & 0x1) != 0;
            outBits.set(i, b);
        }
        return true;
    }

private:

    TS7Client client;

    template <typename T>
    T toNetworkByteOrder(T value) {
        static_assert(std::is_integral<T>::value, "T must be an integral type");

        if constexpr (sizeof(T) == 1) {
            // 1 字节的数据无需转换
            return value;
        }
        else if constexpr (sizeof(T) == 2) {
            // 2 字节的数据使用 htons
            return htons(value);
        }
        else if constexpr (sizeof(T) == 4) {
            // 4 字节的数据使用 htonl
            return htonl(value);
        }
        else {
            static_assert(sizeof(T) <= 4, "Unsupported integral type size");
        }
    }

    bool isLittleEndian() {
#if __cpp_lib_endian
        return std::endian::native == std::endian::little;
#else
        uint16_t number = 0x1;
        return *reinterpret_cast<uint8_t*>(&number) == 0x1;
#endif
    }
};


#endif // PLCCONTROL_H
