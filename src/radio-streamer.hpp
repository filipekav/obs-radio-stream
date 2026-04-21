#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>

class RadioStreamer {
public:
    RadioStreamer();
    ~RadioStreamer();

    bool connect(const std::string& host, int port, const std::string& mount,
                 const std::string& user, const std::string& pass, int bitrate);
    void disconnect();
    
    void push_audio(const uint8_t* data, size_t size);

    bool is_connected() const { return connected.load(); }

private:
    void worker_thread();

    std::string m_host;
    int m_port;
    std::string m_mount;
    std::string m_user;
    std::string m_pass;
    int m_bitrate;

    std::atomic<bool> connected{false};
    std::atomic<bool> running{false};

    std::thread thread_handle;
    
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::queue<std::vector<uint8_t>> audio_queue;
};

