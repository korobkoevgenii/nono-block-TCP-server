#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <memory>
#include <fstream>
#include <filesystem>
#include <chrono> 
#include <ctime>
#include <thread>
#include <mutex>
#include <unordered_set>

/// @brief Функция читает из сокета до тех пор, пока есть что читать или пока не прочитали нужное количество данных
/// @param fd id сокета с которого читаем
/// @param buf куда считываем
/// @param numberBytesToRead количество байт больше которого читать не нужно
/// @return сколько байт было прочитано
int readAllData(int fd, std::vector<char> & buf, int numberBytesToRead)
{
    char *buffer = new char[numberBytesToRead];
    int dataWasRead = 0;
    int bytesRead = 0;
    do
    {
        bytesRead = recv(fd, buffer + dataWasRead, numberBytesToRead - dataWasRead, 0);
        dataWasRead += bytesRead;
    } 
    while (dataWasRead != numberBytesToRead && bytesRead > 0);
    
    for(int i=0;i<dataWasRead;i++)
    {
        buf.push_back(buffer[i]);
    }

    delete[] buffer;

    return dataWasRead;
}


/// @brief функция записывает в переданный ей поток текущую дату и время
/// @param fileForLogging поток, в который идет запись
void writeDateTime(std::ofstream & fileForLogging)
{
    auto currentTime = std::chrono::system_clock::now();
    std::time_t readableFormatOfTime = std::chrono::system_clock::to_time_t(currentTime);
    if(fileForLogging.is_open())
    {
        fileForLogging <<std::ctime(&readableFormatOfTime) << " ";
    }
}

/// @brief код ошибки, когда не удалось открыть файл
constexpr int cannotOpenFile = -1;
/// мьютекс для ограничения доступа к записи в файл
std::mutex m_mutexForLogging;

/// @brief класс для логгирования подключений, записывает дату, время и данные, которые пришли
class Logger
{
 private:
    std::string m_url;  ///< путь к папке, в которой хотим создать/открыть файл
    std::string m_name; ///< имя файла, который хотим создать/открыть

 public:
    /// @brief конструктор по умолчанию
    /// @param url путь к папке
    /// @param name  имя файла
    Logger(const std::string & url, const std::string & name)
    : m_url(url)
    , m_name(name)
    {
    }


    /// @brief функция логгирования данных
    /// @param dataForLogging данные, которые нужно логгировать
    /// @return возвращает 0, если все прошло успешно и -1, если не удалось открыть файл
    int logConnection(const std::vector<char> dataForLogging)
    {

        m_mutexForLogging.lock();

        std::filesystem::path pathToFile(m_url + m_name);
        std::ofstream fileForLogging;

        fileForLogging.open(pathToFile,std::ios::app);
        bool isOpen = fileForLogging.is_open();

        if(isOpen)
        {
            std::string stringForPrint(dataForLogging.begin(), dataForLogging.end());

            writeDateTime(fileForLogging);
            fileForLogging << stringForPrint << "\n";
        }

        fileForLogging.close();

        m_mutexForLogging.unlock();
        return isOpen ? 0 : -1;
    }
};

int main()
{
    Logger logger{"","log-file.txt"};
    int listener;
    sockaddr_in addr;

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if(listener < 0)
    {
        std::cout << "socket was not created";
        return -1;
    }
    
    fcntl(listener, F_SETFL, O_NONBLOCK);
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5000);
    addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::cout << "bind was not made";
        return -2;
    }

    listen(listener, 2);
    
    std::unordered_set<int> clients;
    clients.clear();

    while(true)
    {
        // Заполняем множество сокетов
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(listener, &readset);

        for(auto it : clients)
            FD_SET(it, &readset);

        // Задаём таймаут
        timeval timeout;
        timeout.tv_sec = 15000;
        timeout.tv_usec = 0;

        // Ждём события в одном из сокетов
        int max = std::max(listener, clients.empty() ? 0 : *std::max_element(clients.begin(), clients.end()));
        if(select(max+1, &readset, NULL, NULL, &timeout) <= 0)
        {
            std::cout << "select was not made \n";
            continue;
        }
        
        // Определяем тип события и выполняем соответствующие действия
        if(FD_ISSET(listener, &readset))
        {
            // Поступил новый запрос на соединение, используем accept
            int sock = accept(listener, NULL, NULL);
            if(sock < 0)
            {
                std::cout << "accept failed\n";
            }
            fcntl(sock, F_SETFL, O_NONBLOCK);

            clients.insert(sock);
        }
        
        if(clients.empty())
            continue;

        for(auto it = clients.begin(); it != clients.end(); it++)
        {
            if(FD_ISSET(*it, &readset))
            {
                std::vector<char> buf{};
                // Поступили данные от клиента, читаем их
                int bytesRead = readAllData(*it, buf, 1024);
                
                if(bytesRead <= 0)
                {
                    // Соединение разорвано, удаляем сокет из множества
                    close(*it);
                    clients.erase(it);

                    break;
                }
                else
                {
                    std::thread threadForLogging(&Logger::logConnection, logger, buf);
                    threadForLogging.detach();

                    send(*it, buf.data(), bytesRead, 0);
                }
            }
        }
    }
    
    return 0;
}