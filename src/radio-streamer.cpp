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
                            const std::string& user, const std::string& pass, int bitrate) {
    if (connected.load()) return true;

    m_host = host;
    m_port = port;
    m_mount = mount;
    m_user = user;
    m_pass = pass;
    m_bitrate = bitrate;

    connected = true;
    running = true;
    thread_handle = std::thread(&RadioStreamer::worker_thread, this);

    blog(LOG_INFO, "[Radio] Iniciando conexão ao servidor Icecast %s:%d%s", host.c_str(), port, mount.c_str());
    return true;
}

void RadioStreamer::disconnect() {
    if (!running.load()) return;

    running = false;
    queue_cv.notify_one();

    if (thread_handle.joinable()) {
        thread_handle.join();
    }

    connected = false;
    
    std::lock_guard<std::mutex> lock(queue_mutex);
    while(!audio_queue.empty()) audio_queue.pop();

    blog(LOG_INFO, "[Radio] Desconectado do Icecast");
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
    
    socket.connectToHost(QString::fromStdString(m_host), m_port);
    if (!socket.waitForConnected(5000)) {
        blog(LOG_ERROR, "[Radio] Erro de conexão com %s:%d - %s", 
             m_host.c_str(), m_port, socket.errorString().toStdString().c_str());
        connected = false;
        running = false;
        return;
    }

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

    blog(LOG_INFO, "[Radio] Enviando Header Handshake para Icecast:\n%s", header.toStdString().c_str());

    socket.write(header.toUtf8());
    if (!socket.waitForBytesWritten(3000)) {
        blog(LOG_ERROR, "[Radio] Erro ao enviar Header Handshake: %s", socket.errorString().toStdString().c_str());
        connected = false;
        running = false;
        return;
    }

    // Leitura e validação da resposta do servidor
    if (!socket.waitForReadyRead(5000)) {
        blog(LOG_ERROR, "[Radio] Timeout aguardando resposta do servidor Icecast: %s", socket.errorString().toStdString().c_str());
        connected = false;
        running = false;
        return;
    }

    QByteArray response = socket.readAll();
    QString responseStr = QString::fromUtf8(response);
    blog(LOG_INFO, "[Radio] Resposta Recebida do Servidor:\n%s", responseStr.toStdString().c_str());

    if (!responseStr.contains("200 OK", Qt::CaseInsensitive) && 
        !responseStr.contains("100 Continue", Qt::CaseInsensitive)) {
        blog(LOG_ERROR, "[Radio] Acesso negado ou erro no handshake do servidor. Abortando transmissão.");
        connected = false;
        running = false;
        socket.disconnectFromHost();
        return;
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
            socket.write((const char*)chunk.data(), chunk.size());
            if (!socket.waitForBytesWritten(3000)) {
                blog(LOG_ERROR, "[Radio] Dropando audio. Falha na escrita do socket: %s", socket.errorString().toStdString().c_str());
                break;
            }
        }
    }
    
    socket.disconnectFromHost();
    if (socket.state() != QAbstractSocket::UnconnectedState) {
         socket.waitForDisconnected(1000);
    }
}

