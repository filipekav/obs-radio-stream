#include "radio-streamer.hpp"
#include <obs-module.h>
#include <QTcpSocket>
#include <QString>
#include <QByteArray>
#include <QAbstractSocket>
#include <QNetworkProxy>
#include <QThread>

RadioStreamer* RadioStreamer::s_active_streamer = nullptr;

// ─── Interruptible wait helpers ──────────────────────────────────────────────
// Break long blocking waits into short 200ms slots, checking the running flag
// between each slot. This ensures disconnect() can interrupt any wait within
// ~200ms instead of waiting up to 15 seconds.

bool RadioStreamer::interruptible_wait_connected(QTcpSocket& socket, int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms && running.load()) {
        int wait = std::min(WAIT_SLOT_MS, timeout_ms - elapsed);
        if (socket.waitForConnected(wait)) return true;
        // Check if actual error (not just timeout)
        if (socket.state() == QAbstractSocket::UnconnectedState) return false;
        elapsed += wait;
    }
    return socket.state() == QAbstractSocket::ConnectedState;
}

bool RadioStreamer::interruptible_wait_bytes_written(QTcpSocket& socket, int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms && running.load()) {
        int wait = std::min(WAIT_SLOT_MS, timeout_ms - elapsed);
        if (socket.waitForBytesWritten(wait)) return true;
        if (socket.state() == QAbstractSocket::UnconnectedState) return false;
        elapsed += wait;
    }
    return false;
}

bool RadioStreamer::interruptible_wait_ready_read(QTcpSocket& socket, int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms && running.load()) {
        int wait = std::min(WAIT_SLOT_MS, timeout_ms - elapsed);
        if (socket.waitForReadyRead(wait)) return true;
        if (socket.state() == QAbstractSocket::UnconnectedState) return false;
        elapsed += wait;
    }
    return false;
}

// ─── Helper: drain audio queue to local recording file ───────────────────────

void RadioStreamer::drain_queue_to_file() {
    std::lock_guard<std::mutex> lock(queue_mutex);
    while (!audio_queue.empty()) {
        auto& chunk = audio_queue.front();
        if (recordFile.is_open()) {
            recordFile.write((const char*)chunk.data(), chunk.size());
        }
        audio_queue.pop();
    }
}

// ─── Helper: safely close/abort socket ───────────────────────────────────────

void RadioStreamer::cleanup_socket(QTcpSocket& socket) {
    // abort() is non-blocking and immediately closes the socket,
    // unlike disconnectFromHost() + waitForDisconnected() which can block.
    socket.abort();
}

// ─── Constructor / Destructor ────────────────────────────────────────────────

RadioStreamer::RadioStreamer() {
    s_active_streamer = this;
}

RadioStreamer::~RadioStreamer() {
    disconnect();
    if (s_active_streamer == this) {
        s_active_streamer = nullptr;
    }
}

// ─── Start / State Update / Disconnect ───────────────────────────────────────

bool RadioStreamer::start(const std::string& host, int port, const std::string& mount,
                          const std::string& user, const std::string& pass, int bitrate,
                          int protocol_type, bool stream, bool record, const std::string& recordPath) {
    if (running.load()) {
        update_state(stream, record, recordPath);
        return true;
    }

    if (thread_handle.joinable()) {
        thread_handle.join();
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        while(!audio_queue.empty()) audio_queue.pop();
    }

    {
        std::lock_guard<std::mutex> lock(settings_mutex);
        m_host = host;
        m_port = port;
        m_mount = mount;
        m_user = user;
        m_pass = pass;
        m_bitrate = bitrate;
        m_protocol_type = protocol_type;
        m_path = recordPath;
    }

    stream_connected = false;
    recording_active = false;
    reconnecting = false;
    record_flush_counter = 0;
    
    m_stream_active = stream;
    m_record_active = record;
    
    running = true;
    thread_handle = std::thread(&RadioStreamer::worker_thread, this);

    return true;
}

void RadioStreamer::update_state(bool stream, bool record, const std::string& recordPath) {
    {
        std::lock_guard<std::mutex> lock(settings_mutex);
        if (!recordPath.empty()) {
            m_path = recordPath;
        }
    }
    m_stream_active = stream;
    m_record_active = record;
    queue_cv.notify_one();
}

void RadioStreamer::disconnect() {
    running = false;
    m_stream_active = false;
    m_record_active = false;
    queue_cv.notify_one();

    if (thread_handle.joinable()) {
        thread_handle.join();
    }

    stream_connected = false;
    recording_active = false;
    reconnecting = false;
    
    std::lock_guard<std::mutex> lock(queue_mutex);
    while(!audio_queue.empty()) audio_queue.pop();

    blog(LOG_INFO, "%s", obs_module_text("LogDisconnected"));
}

// ─── Push audio (called from OBS audio thread) ──────────────────────────────

void RadioStreamer::push_audio(const uint8_t* data, size_t size) {
    if (!running.load() || !data || size == 0) return;

    std::vector<uint8_t> buffer(data, data + size);
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        
        // Enforce queue size limit to prevent unbounded memory growth.
        // Drop oldest chunks if queue is full (audio is being produced
        // faster than it can be consumed, e.g. during reconnection).
        while (audio_queue.size() >= MAX_QUEUE_SIZE) {
            audio_queue.pop();
        }
        
        audio_queue.push(std::move(buffer));
    }
    queue_cv.notify_one();
}

// ─── Connection attempt (Icecast / SHOUTcast handshake) ─────────────────────

bool RadioStreamer::attempt_connection(QTcpSocket& socket) {
    std::string host;
    int port;
    std::string mount;
    std::string user;
    std::string pass;
    int bitrate;
    int protocol_type;

    {
        std::lock_guard<std::mutex> lock(settings_mutex);
        host = m_host;
        port = m_port;
        mount = m_mount;
        user = m_user;
        pass = m_pass;
        bitrate = m_bitrate;
        protocol_type = m_protocol_type;
    }

    // SHOUTcast v1 DNAS: source/DJ clients connect on port+1
    int connect_port = port;
    if (protocol_type == 1) {
        connect_port = port + 1;
    }

    QString log_mount = (protocol_type == 0) ? QString::fromStdString(mount) : "";
    blog(LOG_INFO, obs_module_text("LogConnecting"), host.c_str(), connect_port, log_mount.toStdString().c_str());

    socket.setProxy(QNetworkProxy::NoProxy); // Bypass Windows Proxies
    QString clean_host = QString::fromStdString(host).trimmed();
    socket.connectToHost(clean_host, connect_port);

    if (!interruptible_wait_connected(socket, 15000)) {
        blog(LOG_ERROR, obs_module_text("LogErrorConnection"), 
             clean_host.toStdString().c_str(), connect_port, socket.errorString().toStdString().c_str());
        cleanup_socket(socket);
        return false;
    }

    // Early exit if shutting down (connected but running was set to false during wait)
    if (!running.load() || !m_stream_active.load()) {
        cleanup_socket(socket);
        return false;
    }

    if (protocol_type == 0) {
        // ── Icecast / AzuraCast protocol ──
        QString auth_str = QString::fromStdString(user + ":" + pass);
        QByteArray auth_b64 = auth_str.toUtf8().toBase64();
        
        QString mt = QString::fromStdString(mount);
        if (!mt.startsWith("/")) {
            mt = "/" + mt;
        }

        QString header = QString("PUT %1 HTTP/1.0\r\n"
                                 "Authorization: Basic %2\r\n"
                                 "Content-Type: audio/mpeg\r\n"
                                 "Ice-Name: OBS Radio Stream\r\n"
                                 "Ice-Bitrate: %3\r\n\r\n")
                                 .arg(mt)
                                  .arg(QString(auth_b64))
                                 .arg(bitrate);

        blog(LOG_INFO, obs_module_text("LogSendingHeader"), header.toStdString().c_str());

        socket.write(header.toUtf8());
        if (!interruptible_wait_bytes_written(socket, 3000)) {
            blog(LOG_ERROR, obs_module_text("LogErrorHeader"), socket.errorString().toStdString().c_str());
            cleanup_socket(socket);
            return false;
        }

        // Leitura e validação da resposta do servidor
        if (!interruptible_wait_ready_read(socket, 15000)) {
            blog(LOG_ERROR, obs_module_text("LogErrorTimeout"), socket.errorString().toStdString().c_str());
            cleanup_socket(socket);
            return false;
        }

        QByteArray response = socket.readAll();
        QString responseStr = QString::fromUtf8(response);
        blog(LOG_INFO, obs_module_text("LogResponseReceived"), responseStr.toStdString().c_str());

        if (!responseStr.contains("200 OK", Qt::CaseInsensitive) && 
            !responseStr.contains("100 Continue", Qt::CaseInsensitive)) {
            blog(LOG_ERROR, "%s", obs_module_text("LogErrorAuth"));
            cleanup_socket(socket);
            return false;
        }
    } else {
        // ── SHOUTcast v1 protocol ──
        QString pass_header = QString::fromStdString(pass).trimmed() + "\r\n";
        socket.write(pass_header.toUtf8());

        if (!interruptible_wait_bytes_written(socket, 15000)) {
            blog(LOG_ERROR, obs_module_text("LogErrorTimeout"), socket.errorString().toStdString().c_str());
            cleanup_socket(socket);
            return false;
        }

        if (!interruptible_wait_ready_read(socket, 15000)) {
            blog(LOG_ERROR, obs_module_text("LogErrorTimeout"), socket.errorString().toStdString().c_str());
            cleanup_socket(socket);
            return false;
        }

        QByteArray response = socket.readAll();
        QString responseStr = QString::fromUtf8(response);
        blog(LOG_INFO, obs_module_text("LogResponseReceived"), responseStr.toStdString().c_str());

        if (!responseStr.contains("OK2", Qt::CaseInsensitive)) {
            blog(LOG_ERROR, "%s", obs_module_text("LogErrorAuth"));
            cleanup_socket(socket);
            return false;
        }

        // Send required ICY headers for SHOUTcast v1
        QString icy_headers = QString("icy-name: OBS Radio Stream\r\n"
                                      "icy-genre: Live Broadcast\r\n"
                                      "icy-br: %1\r\n"
                                      "icy-pub: 0\r\n\r\n").arg(bitrate);
        socket.write(icy_headers.toUtf8());
        interruptible_wait_bytes_written(socket, 5000);
    }

    return true;
}

// ─── Helper: reconnection loop ──────────────────────────────────────────────

bool RadioStreamer::try_reconnect(QTcpSocket& socket) {
    if (recordFile.is_open()) {
        blog(LOG_INFO, "%s", obs_module_text("LogRecordingContinues"));
    }

    for (int attempt = 1; attempt <= MAX_RETRIES && running.load() && m_stream_active.load(); ++attempt) {
        reconnecting = true;
        reconnect_attempt = attempt;
        blog(LOG_INFO, obs_module_text("LogReconnecting"), attempt, MAX_RETRIES);
        if (on_reconnecting_callback) on_reconnecting_callback(attempt, MAX_RETRIES);

        // Wait before retry, draining audio to local file during wait
        for (int waited = 0; waited < RETRY_DELAY_MS && running.load() && m_stream_active.load(); waited += 100) {
            QThread::msleep(100);
            drain_queue_to_file();
        }
        if (!running.load() || !m_stream_active.load()) break;

        if (attempt_connection(socket)) {
            reconnecting = false;
            reconnect_attempt = 0;
            stream_connected = true;
            blog(LOG_INFO, "%s", obs_module_text("LogReconnected"));
            if (on_reconnected_callback) on_reconnected_callback();
            return true;
        }
    }

    reconnecting = false;
    reconnect_attempt = 0;
    return false;
}

// ─── Worker thread (main loop) ──────────────────────────────────────────────

void RadioStreamer::worker_thread() {
    QTcpSocket socket;

    while (running.load()) {
        // ── 1. Manage Local Recording File State ──
        bool want_record = m_record_active.load();
        if (want_record && !recordFile.is_open()) {
            std::string path_to_open;
            {
                std::lock_guard<std::mutex> lock(settings_mutex);
                path_to_open = m_path;
            }
            if (!path_to_open.empty()) {
                recordFile.open(path_to_open, std::ios::binary);
                if (!recordFile.is_open()) {
                    blog(LOG_WARNING, obs_module_text("LogErrorFileOpen"), path_to_open.c_str());
                    recording_active = false;
                } else {
                    blog(LOG_INFO, obs_module_text("LogLocalRecordingEnabled"), path_to_open.c_str());
                    recording_active = true;
                }
            }
        } else if (!want_record && recordFile.is_open()) {
            recordFile.flush();
            recordFile.close();
            recording_active = false;
            blog(LOG_INFO, "%s", obs_module_text("LogRecordingFinished"));
        }

        // ── 2. Manage Streaming Connection State ──
        bool want_stream = m_stream_active.load();
        if (want_stream && !stream_connected.load() && !reconnecting.load()) {
            if (!attempt_connection(socket)) {
                // Initial connection failed, start try_reconnect
                if (!try_reconnect(socket)) {
                    m_stream_active = false;
                    stream_connected = false;
                    if (on_disconnect_callback) on_disconnect_callback();
                }
            } else {
                stream_connected = true;
            }
        } else if (!want_stream && stream_connected.load()) {
            cleanup_socket(socket);
            stream_connected = false;
        }

        // If neither is active and no reconnection is pending, we exit
        if (!want_record && !want_stream && !reconnecting.load()) {
            running = false;
            break;
        }

        // ── 3. Read/Process Audio Queue ──
        std::vector<uint8_t> chunk;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            // Wait for queue or a change in target state or running flag
            queue_cv.wait_for(lock, std::chrono::milliseconds(200), [this]() {
                return !audio_queue.empty() || !running.load() || 
                       m_record_active.load() != recording_active.load() ||
                       m_stream_active.load() != stream_connected.load();
            });
            
            if (!running.load()) break;
            
            if (!audio_queue.empty()) {
                chunk = std::move(audio_queue.front());
                audio_queue.pop();
            }
        }

        if (!chunk.empty()) {
            if (recordFile.is_open()) {
                recordFile.write((const char*)chunk.data(), chunk.size());
                
                // Periodic flush
                if (++record_flush_counter >= FLUSH_INTERVAL) {
                    recordFile.flush();
                    record_flush_counter = 0;
                }
            }

            if (stream_connected.load()) {
                socket.write((const char*)chunk.data(), chunk.size());
                if (!interruptible_wait_bytes_written(socket, 3000)) {
                    blog(LOG_ERROR, obs_module_text("LogErrorSocketWrite"), socket.errorString().toStdString().c_str());
                    stream_connected = false;
                    cleanup_socket(socket);

                    if (!try_reconnect(socket)) {
                        blog(LOG_ERROR, "%s", obs_module_text("LogReconnectFailed"));
                        m_stream_active = false;
                        if (on_disconnect_callback) on_disconnect_callback();
                    }
                }
            }
        }
    }
    
    // ── 4. Thread Exit Cleanup ──
    if (stream_connected.load()) {
        cleanup_socket(socket);
    }
    stream_connected = false;
    
    if (recordFile.is_open()) {
        recordFile.flush();
        recordFile.close();
        recording_active = false;
        blog(LOG_INFO, "%s", obs_module_text("LogRecordingFinished"));
    }
}

