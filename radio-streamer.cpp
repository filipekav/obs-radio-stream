#include "radio-streamer.hpp"
#include <obs-module.h>

RadioStreamer::RadioStreamer() {
    shout_init();
}

RadioStreamer::~RadioStreamer() {
    disconnect();
    shout_shutdown();
}

bool RadioStreamer::connect(const std::string& host, int port, const std::string& mount,
                            const std::string& user, const std::string& pass, int bitrate) {
    if (connected.load()) return true;

    shout = shout_new();
    if (!shout) {
        blog(LOG_ERROR, "[Radio] Falha ao alocar shout_t");
        return false;
    }

    shout_set_host(shout, host.c_str());
    shout_set_port(shout, port);
    shout_set_mount(shout, mount.c_str());
    shout_set_user(shout, user.c_str());
    shout_set_password(shout, pass.c_str());
    shout_set_format(shout, SHOUT_FORMAT_MP3);
    shout_set_protocol(shout, SHOUT_PROTOCOL_HTTP);
    
    std::string bitrate_str = std::to_string(bitrate);
    shout_set_audio_info(shout, SHOUT_AI_BITRATE, bitrate_str.c_str());

    if (shout_open(shout) != SHOUTERR_SUCCESS) {
        blog(LOG_ERROR, "[Radio] Falha ao conectar ao Icecast: %s", shout_get_error(shout));
        shout_free(shout);
        shout = nullptr;
        return false;
    }

    connected = true;
    running = true;
    thread_handle = std::thread(&RadioStreamer::worker_thread, this);

    blog(LOG_INFO, "[Radio] Conectado ao servidor Icecast %s:%d%s", host.c_str(), port, mount.c_str());
    return true;
}

void RadioStreamer::disconnect() {
    if (!running.load()) return;

    running = false;
    queue_cv.notify_one();

    if (thread_handle.joinable()) {
        thread_handle.join();
    }

    if (shout) {
        shout_close(shout);
        shout_free(shout);
        shout = nullptr;
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
            int ret = shout_send(shout, chunk.data(), chunk.size());
            if (ret != SHOUTERR_SUCCESS) {
                blog(LOG_ERROR, "[Radio] Erro shout_send: %s", shout_get_error(shout));
                // Parar se der erro
                running = false;
                connected = false;
            }
            shout_sync(shout);
        }
    }
}
