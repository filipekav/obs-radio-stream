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

    bool start(const std::string& host, int port, const std::string& mount,
               const std::string& user, const std::string& pass, int bitrate,
               int protocol_type, bool stream, bool record, const std::string& recordPath);
    void update_state(bool stream, bool record, const std::string& recordPath = "");
    void disconnect();
    
    void push_audio(const uint8_t* data, size_t size);

    bool is_connected() const { return stream_connected.load(); }
    bool is_recording() const { return recording_active.load(); }
    bool is_reconnecting() const { return reconnecting.load(); }
    int get_reconnect_attempt() const { return reconnect_attempt.load(); }
    int get_reconnect_max() const { return MAX_RETRIES; }
    bool is_running() const { return running.load(); }

    static RadioStreamer* get_active_streamer() { return s_active_streamer; }

private:
    static RadioStreamer* s_active_streamer;
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

    std::mutex settings_mutex;
    std::string m_host;
    int m_port = 0;
    std::string m_mount;
    std::string m_user;
    std::string m_pass;
    int m_bitrate = 128;
    std::string m_path;
    int m_protocol_type = 0;
    
    std::ofstream recordFile;
    int record_flush_counter = 0;

    std::atomic<bool> stream_connected{false};
    std::atomic<bool> recording_active{false};
    std::atomic<bool> running{false};
    std::atomic<bool> reconnecting{false};
    std::atomic<int> reconnect_attempt{0};
    
    std::atomic<bool> m_stream_active{false};
    std::atomic<bool> m_record_active{false};

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
