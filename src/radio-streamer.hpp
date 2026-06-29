#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <functional>
#include <fstream>

class QTcpSocket;

class RadioStreamer {
public:
    std::function<void()> on_disconnect_callback;
    std::function<void(int attempt, int max)> on_reconnecting_callback;
    std::function<void()> on_reconnected_callback;
    
    RadioStreamer();
    ~RadioStreamer();

    bool connect(const std::string& host, int port, const std::string& mount,
                 const std::string& user, const std::string& pass, int bitrate,
                 bool recordLocally, const std::string& recordingPath, int protocol_type);
    void disconnect();
    
    void push_audio(const uint8_t* data, size_t size);

    bool is_connected() const { return stream_connected.load(); }
    bool is_running() const { return running.load(); }

private:
    void worker_thread();
    bool attempt_connection(QTcpSocket& socket);
    void drain_queue_to_file();
    void cleanup_socket(QTcpSocket& socket);
    bool try_reconnect(QTcpSocket& socket);

    // Interruptible wait helpers — break long waits into 200ms slots
    // checking running flag between each slot. Returns immediately if running becomes false.
    bool interruptible_wait_connected(QTcpSocket& socket, int timeout_ms);
    bool interruptible_wait_bytes_written(QTcpSocket& socket, int timeout_ms);
    bool interruptible_wait_ready_read(QTcpSocket& socket, int timeout_ms);

    std::string m_host;
    int m_port = 0;
    std::string m_mount;
    std::string m_user;
    std::string m_pass;
    int m_bitrate = 128;
    bool m_record = false;
    std::string m_path;
    int m_protocol_type = 0;
    
    std::ofstream recordFile;
    int record_flush_counter = 0;

    std::atomic<bool> stream_connected{false};
    std::atomic<bool> running{false};
    std::atomic<bool> reconnecting{false};

    static constexpr int MAX_RETRIES = 5;
    static constexpr int RETRY_DELAY_MS = 5000;
    static constexpr int WAIT_SLOT_MS = 200;
    static constexpr size_t MAX_QUEUE_SIZE = 500;
    static constexpr int FLUSH_INTERVAL = 50;  // flush every 50 chunks (~1s at 48kHz)

    std::thread thread_handle;
    
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::queue<std::vector<uint8_t>> audio_queue;
};
