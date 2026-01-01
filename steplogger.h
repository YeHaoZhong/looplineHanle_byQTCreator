#ifndef STEPLOGGER_H
#define STEPLOGGER_H

#include <string>
#include <fstream>
#include <mutex>
class StepLogger {
public:

    static StepLogger& getInstance();
    ~StepLogger();

    void Log(const std::string& message);
    void close();

private:
    StepLogger();
    StepLogger(const StepLogger&) = delete;
    StepLogger& operator=(const StepLogger&) = delete;

    std::string getLogFileName();            // "YYYY-MM-DD_HH.txt"
    std::string getCurrentTime();            // "YYYY-MM-DD HH:MM:SS.mmm"
    std::string getCurrentDate();            // "YYYY-MM-DD"
    void rotateLogFileIfNeeded();
    void ensureLogDirectory();

    static StepLogger* instance;
    std::mutex logMutex;
    std::ofstream logFile;
    std::string logRootDir;   // base ".../logs"
    std::string logDayDir;    // ".../logs/YYYY-MM-DD"
    std::string logFilePath;
    std::string currentDate;
    int currentHour;
};

#endif // STEPLOGGER_H
