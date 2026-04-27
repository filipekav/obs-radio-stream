#include "radio-streamer.hpp"
#include <obs-module.h>
#include <QTcpSocket>
#include <QString>
#include <QByteArray>
#include <QAbstractSocket>

RadioStreamer::RadioStreamer() {
}

RadioStreamer::~RadioStreamer() {
    disconnect();
}

bool RadioStreamer::connect(const std::string& host, int port, const std::string& mount,
                            const std::string& user, const std::string& pass, int bitrate,
                            bool recordLocally, const std::string& recordingPath, int protocol_type) {
    if (connected.load()) return true;

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

    connected = true;
    running = true;
    thread_handle = std::thread(&RadioStreamer::worker_thread, this);

    blog(LOG_INFO, obs_module_text("LogConnecting"), host.c_str(), port, mount.c_str());
    return true;
}

void RadioStreamer::disconnect() {
    running = false;
    queue_cv.notify_one();

    if (thread_handle.joinable()) {
        thread_handle.join();
    }

    connected = false;
    
    std::lock_guard<std::mutex> lock(queue_mutex);
    while(!audio_queue.empty()) audio_queue.pop();

    blog(LOG_INFO, "%s", obs_module_text("LogDisconnected"));
}

void RadioStreamer::push_audio(const uint8_t* data, size_t size) {
    if (!connected.load() || !data || size == 0) return;

    std::vector<uint8_t> buffer(data, data + size);
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        audio_queue.push(std::move(buffer));
    }
    queue_cv.notify_one();
}

void RadioStreamer::worker_thread() {
    QTcpSocket socket;

    if (m_record && !m_path.empty()) {
        recordFile.open(m_path, std::ios::binary);
        if (!recordFile.is_open()) {
            blog(LOG_WARNING, obs_module_text("LogErrorFileOpen"), m_path.c_str());
        } else {
            blog(LOG_INFO, obs_module_text("LogLocalRecordingEnabled"), m_path.c_str());
        }
    }
    
    socket.connectToHost(QString::fromStdString(m_host), m_port, QIODevice::ReadWrite, QAbstractSocket::IPv4Protocol);
    if (!socket.waitForConnected(15000)) {
        blog(LOG_ERROR, obs_module_text("LogErrorConnection"), 
             m_host.c_str(), m_port, socket.errorString().toStdString().c_str());
        connected = false;
        running = false;
        if (on_disconnect_callback) on_disconnect_callback();
        socket.abort();
        return;
    }

    if (m_protocol_type == 0) {
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
            connected = false;
            running = false;
            if (on_disconnect_callback) on_disconnect_callback();
            return;
        }

        // Leitura e validação da resposta do servidor
        if (!socket.waitForReadyRead(15000)) {
            blog(LOG_ERROR, obs_module_text("LogErrorTimeout"), socket.errorString().toStdString().c_str());
            connected = false;
            running = false;
            if (on_disconnect_callback) on_disconnect_callback();
            return;
        }

        QByteArray response = socket.readAll();
        QString responseStr = QString::fromUtf8(response);
        blog(LOG_INFO, obs_module_text("LogResponseReceived"), responseStr.toStdString().c_str());

        if (!responseStr.contains("200 OK", Qt::CaseInsensitive) && 
            !responseStr.contains("100 Continue", Qt::CaseInsensitive)) {
            blog(LOG_ERROR, "%s", obs_module_text("LogErrorAuth"));
            connected = false;
            running = false;
            socket.disconnectFromHost();
            if (on_disconnect_callback) on_disconnect_callback();
            socket.abort();
            return;
        }
    } else {
        QString pass_header = QString::fromStdString(m_pass) + "\r\n";
        socket.write(pass_header.toUtf8());

        if (!socket.waitForBytesWritten(15000)) {
            blog(LOG_ERROR, obs_module_text("LogErrorHeader"), socket.errorString().toStdString().c_str());
            connected = false;
            running = false;
            if (on_disconnect_callback) on_disconnect_callback();
            return;
        }

        if (!socket.waitForReadyRead(15000)) {
            blog(LOG_ERROR, obs_module_text("LogErrorTimeout"), socket.errorString().toStdString().c_str());
            connected = false;
            running = false;
            if (on_disconnect_callback) on_disconnect_callback();
            return;
        }

        QByteArray response = socket.readAll();
        QString responseStr = QString::fromUtf8(response);
        blog(LOG_INFO, obs_module_text("LogResponseReceived"), responseStr.toStdString().c_str());

        if (!responseStr.contains("OK2", Qt::CaseInsensitive)) {
            blog(LOG_ERROR, "%s", obs_module_text("LogErrorAuth"));
            connected = false;
            running = false;
            socket.disconnectFromHost();
            if (on_disconnect_callback) on_disconnect_callback();
            socket.abort();
            return;
        }

        // Send SHOUTcast v1 ICY headers
        QString icy_headers = QString("icy-name: OBS Radio Stream\r\n"
                                      "icy-genre: Live Broadcast\r\n"
                                      "icy-br: %1\r\n"
                                      "icy-pub: 0\r\n\r\n").arg(m_bitrate);
        socket.write(icy_headers.toUtf8());
        if (!socket.waitForBytesWritten(5000)) {
            blog(LOG_ERROR, "[Radio] Error sending ICY headers.");
        }
    }

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
            if (recordFile.is_open()) {
                recordFile.write((const char*)chunk.data(), chunk.size());
            }

            socket.write((const char*)chunk.data(), chunk.size());
            if (!socket.waitForBytesWritten(3000)) {
                blog(LOG_ERROR, obs_module_text("LogErrorSocketWrite"), socket.errorString().toStdString().c_str());
                if (on_disconnect_callback) on_disconnect_callback();
                break;
            }
        }
    }
    
    socket.disconnectFromHost();
    if (socket.state() != QAbstractSocket::UnconnectedState) {
         socket.waitForDisconnected(1000);
    }
    
    if (recordFile.is_open()) {
        recordFile.close();
        blog(LOG_INFO, "%s", obs_module_text("LogRecordingFinished"));
    }
}

