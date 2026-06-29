#include "radio-streamer.hpp"
#include <obs-module.h>
#include <QTcpSocket>
#include <QString>
#include <QByteArray>
#include <QAbstractSocket>
#include <QNetworkProxy>
#include <QThread>

RadioStreamer::RadioStreamer() {
}

RadioStreamer::~RadioStreamer() {
    disconnect();
}

bool RadioStreamer::connect(const std::string& host, int port, const std::string& mount,
                            const std::string& user, const std::string& pass, int bitrate,
                            bool recordLocally, const std::string& recordingPath, int protocol_type) {
    if (running.load()) return true;

    if (thread_handle.joinable()) {
        thread_handle.join();
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        while(!audio_queue.empty()) audio_queue.pop();
    }

    m_host = host;
    m_port = port;
    m_mount = mount;
    m_user = user;
    m_pass = pass;
    m_bitrate = bitrate;
    m_record = recordLocally;
    m_path = recordingPath;
    m_protocol_type = protocol_type;

    stream_connected = false;
    reconnecting = false;
    running = true;
    thread_handle = std::thread(&RadioStreamer::worker_thread, this);

    return true;
}

void RadioStreamer::disconnect() {
    running = false;
    queue_cv.notify_one();

    if (thread_handle.joinable()) {
        thread_handle.join();
    }

    stream_connected = false;
    reconnecting = false;
    
    std::lock_guard<std::mutex> lock(queue_mutex);
    while(!audio_queue.empty()) audio_queue.pop();

    blog(LOG_INFO, "%s", obs_module_text("LogDisconnected"));
}

void RadioStreamer::push_audio(const uint8_t* data, size_t size) {
    if (!running.load() || !data || size == 0) return;

    std::vector<uint8_t> buffer(data, data + size);
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        audio_queue.push(std::move(buffer));
    }
    queue_cv.notify_one();
}

bool RadioStreamer::attempt_connection(QTcpSocket& socket) {
    // SHOUTcast v1 DNAS: source/DJ clients connect on port+1
    int connect_port = m_port;
    if (m_protocol_type == 1) {
        connect_port = m_port + 1;
    }

    QString log_mount = (m_protocol_type == 0) ? QString::fromStdString(m_mount) : "";
    blog(LOG_INFO, obs_module_text("LogConnecting"), m_host.c_str(), connect_port, log_mount.toStdString().c_str());

    socket.setProxy(QNetworkProxy::NoProxy);
    QString clean_host = QString::fromStdString(m_host).trimmed();
    socket.connectToHost(clean_host, connect_port);

    if (!socket.waitForConnected(15000)) {
        blog(LOG_ERROR, obs_module_text("LogErrorConnection"), 
             clean_host.toStdString().c_str(), connect_port, socket.errorString().toStdString().c_str());
        socket.abort();
        return false;
    }

    if (m_protocol_type == 0) {
        // Icecast / AzuraCast protocol
        QString auth_str = QString::fromStdString(m_user + ":" + m_pass);
        QByteArray auth_b64 = auth_str.toUtf8().toBase64();
        
        QString mt = QString::fromStdString(m_mount);
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
                                 .arg(m_bitrate);

        blog(LOG_INFO, obs_module_text("LogSendingHeader"), header.toStdString().c_str());

        socket.write(header.toUtf8());
        if (!socket.waitForBytesWritten(3000)) {
            blog(LOG_ERROR, obs_module_text("LogErrorHeader"), socket.errorString().toStdString().c_str());
            return false;
        }

        if (!socket.waitForReadyRead(15000)) {
            blog(LOG_ERROR, obs_module_text("LogErrorTimeout"), socket.errorString().toStdString().c_str());
            return false;
        }

        QByteArray response = socket.readAll();
        QString responseStr = QString::fromUtf8(response);
        blog(LOG_INFO, obs_module_text("LogResponseReceived"), responseStr.toStdString().c_str());

        if (!responseStr.contains("200 OK", Qt::CaseInsensitive) && 
            !responseStr.contains("100 Continue", Qt::CaseInsensitive)) {
            blog(LOG_ERROR, "%s", obs_module_text("LogErrorAuth"));
            socket.disconnectFromHost();
            socket.abort();
            return false;
        }
    } else {
        // SHOUTcast v1 protocol
        QString pass_header = QString::fromStdString(m_pass).trimmed() + "\r\n";
        socket.write(pass_header.toUtf8());

        if (!socket.waitForBytesWritten(15000) || !socket.waitForReadyRead(15000)) {
            blog(LOG_ERROR, obs_module_text("LogErrorTimeout"), socket.errorString().toStdString().c_str());
            return false;
        }

        QByteArray response = socket.readAll();
        QString responseStr = QString::fromUtf8(response);
        blog(LOG_INFO, obs_module_text("LogResponseReceived"), responseStr.toStdString().c_str());

        if (!responseStr.contains("OK2", Qt::CaseInsensitive)) {
            blog(LOG_ERROR, "%s", obs_module_text("LogErrorAuth"));
            socket.disconnectFromHost();
            socket.abort();
            return false;
        }

        // Send required ICY headers for SHOUTcast v1
        QString icy_headers = QString("icy-name: OBS Radio Stream\r\n"
                                      "icy-genre: Live Broadcast\r\n"
                                      "icy-br: %1\r\n"
                                      "icy-pub: 0\r\n\r\n").arg(m_bitrate);
        socket.write(icy_headers.toUtf8());
        socket.waitForBytesWritten(5000);
    }

    return true;
}

void RadioStreamer::worker_thread() {
    QTcpSocket socket;

    // Open local recording file (independent of streaming)
    if (m_record && !m_path.empty()) {
        recordFile.open(m_path, std::ios::binary);
        if (!recordFile.is_open()) {
            blog(LOG_WARNING, obs_module_text("LogErrorFileOpen"), m_path.c_str());
        } else {
            blog(LOG_INFO, obs_module_text("LogLocalRecordingEnabled"), m_path.c_str());
        }
    }

    // Initial connection attempt
    if (!attempt_connection(socket)) {
        // Initial connection failed — still keep recording if enabled
        if (recordFile.is_open()) {
            blog(LOG_INFO, "%s", obs_module_text("LogRecordingContinues"));
        }
        
        // Try reconnecting before giving up entirely
        bool reconnected = false;
        for (int attempt = 1; attempt <= MAX_RETRIES && running.load(); ++attempt) {
            reconnecting = true;
            blog(LOG_INFO, obs_module_text("LogReconnecting"), attempt, MAX_RETRIES);
            if (on_reconnecting_callback) on_reconnecting_callback(attempt, MAX_RETRIES);

            // Wait before retry, draining audio to local file
            for (int waited = 0; waited < RETRY_DELAY_MS && running.load(); waited += 100) {
                QThread::msleep(100);
                
                // Drain queue to local file during wait
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    while (!audio_queue.empty()) {
                        auto& queued = audio_queue.front();
                        if (recordFile.is_open()) {
                            recordFile.write((const char*)queued.data(), queued.size());
                        }
                        audio_queue.pop();
                    }
                }
            }
            if (!running.load()) break;

            if (attempt_connection(socket)) {
                reconnected = true;
                reconnecting = false;
                stream_connected = true;
                blog(LOG_INFO, "%s", obs_module_text("LogReconnected"));
                if (on_reconnected_callback) on_reconnected_callback();
                break;
            }
        }

        if (!reconnected) {
            reconnecting = false;
            running = false;
            stream_connected = false;
            if (recordFile.is_open()) {
                recordFile.close();
                blog(LOG_INFO, "%s", obs_module_text("LogRecordingFinished"));
            }
            if (on_disconnect_callback) on_disconnect_callback();
            return;
        }
    } else {
        stream_connected = true;
    }

    // Main audio loop
    while (running.load()) {
        std::vector<uint8_t> chunk;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this]() { return !audio_queue.empty() || !running.load(); });
            
            if (!running.load() && audio_queue.empty()) break;
            
            if (!audio_queue.empty()) {
                chunk = std::move(audio_queue.front());
                audio_queue.pop();
            }
        }

        if (!chunk.empty()) {
            // Always write to local recording regardless of stream state
            if (recordFile.is_open()) {
                recordFile.write((const char*)chunk.data(), chunk.size());
            }

            // Only send to socket if stream is connected
            if (stream_connected.load()) {
                socket.write((const char*)chunk.data(), chunk.size());
                if (!socket.waitForBytesWritten(3000)) {
                    blog(LOG_ERROR, obs_module_text("LogErrorSocketWrite"), socket.errorString().toStdString().c_str());
                    stream_connected = false;

                    // Close broken socket
                    socket.disconnectFromHost();
                    if (socket.state() != QAbstractSocket::UnconnectedState) {
                        socket.waitForDisconnected(1000);
                    }

                    if (recordFile.is_open()) {
                        blog(LOG_INFO, "%s", obs_module_text("LogRecordingContinues"));
                    }

                    // Reconnection loop
                    bool reconnected = false;
                    for (int attempt = 1; attempt <= MAX_RETRIES && running.load(); ++attempt) {
                        reconnecting = true;
                        blog(LOG_INFO, obs_module_text("LogReconnecting"), attempt, MAX_RETRIES);
                        if (on_reconnecting_callback) on_reconnecting_callback(attempt, MAX_RETRIES);

                        // Wait before retry, draining audio to local file
                        for (int waited = 0; waited < RETRY_DELAY_MS && running.load(); waited += 100) {
                            QThread::msleep(100);
                            
                            // Drain queue to local file during wait
                            {
                                std::lock_guard<std::mutex> lock(queue_mutex);
                                while (!audio_queue.empty()) {
                                    auto& queued = audio_queue.front();
                                    if (recordFile.is_open()) {
                                        recordFile.write((const char*)queued.data(), queued.size());
                                    }
                                    audio_queue.pop();
                                }
                            }
                        }
                        if (!running.load()) break;

                        if (attempt_connection(socket)) {
                            reconnected = true;
                            reconnecting = false;
                            stream_connected = true;
                            blog(LOG_INFO, "%s", obs_module_text("LogReconnected"));
                            if (on_reconnected_callback) on_reconnected_callback();
                            break;
                        }
                    }

                    if (!reconnected) {
                        reconnecting = false;
                        blog(LOG_ERROR, "%s", obs_module_text("LogReconnectFailed"));
                        if (on_disconnect_callback) on_disconnect_callback();
                        break;
                    }
                }
            }
        }
    }
    
    // Cleanup: disconnect socket
    if (stream_connected.load()) {
        socket.disconnectFromHost();
        if (socket.state() != QAbstractSocket::UnconnectedState) {
             socket.waitForDisconnected(1000);
        }
    }
    stream_connected = false;
    
    // Close local recording
    if (recordFile.is_open()) {
        recordFile.close();
        blog(LOG_INFO, "%s", obs_module_text("LogRecordingFinished"));
    }
}
