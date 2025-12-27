#ifndef SOCKETCLINET_H
#define SOCKETCLINET_H
#include <string>
#include <WinSock2.h>
#include <QObject>
#include <atomic>
#include <thread>
class SocketClient :public QObject{

    Q_OBJECT
public:

    SOCKET mSock = INVALID_SOCKET;

    SOCKET ConnectTo(const std::string& ip, int port, bool recevice = true);

    bool sendData(SOCKET sock, const std::string& message);

    void startReceiveData(SOCKET sock);

    void stopReceiveData();

    bool SocketConnection = false;

public slots:

    void sendComand(int socketIndex, const QByteArray& command) {
        if (SocketConnection)
        {
            Q_UNUSED(socketIndex);
            bool ok = sendData(mSock, command.toStdString());
        }
    }
signals:

    void dataReceived(const QByteArray& data);

private:

    void receiveData(SOCKET sock);
    std::thread receiveThread;
    std::atomic<bool> receiving{ false }; // class member: replace bool receiving
    std::string ipAddress;
    int mPort;
};

struct SocketConnection
{
    SocketClient client;
    SOCKET sock = INVALID_SOCKET;
    bool isConnected = false;
    bool connectTo(const std::string& ip, int port)
    {
        sock = client.ConnectTo(ip, port);
        isConnected = (sock != INVALID_SOCKET);
        return isConnected;
    }
    void disconnect()
    {
        if (isConnected)
        {
            client.stopReceiveData();
            closesocket(sock);
            sock = INVALID_SOCKET;
            isConnected = false;
        }
    }
    bool send(const std::string& msg)
    {
        if (client.SocketConnection)
        {
            if (client.sendData(sock, msg))
            {
                return true;
            }
        }
        return false;
    }
};

#endif // SOCKETCLINET_H
