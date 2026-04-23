#include "radio-output.hpp"
#include "radio-streamer.hpp"
#include <lame/lame.h>
#include <vector>

struct radio_output_data {
    obs_output_t* output;
    RadioStreamer* streamer;
    lame_t lame;

    // Buffer for 16-bit interleaved PCM
    std::vector<int16_t> pcm_buffer;
    
    // Settings
    std::string host;
    int port;
    std::string mount;
    std::string user;
    std::string pass;
    int bitrate;
    bool record_locally;
    std::string record_path;
};

static const char* radio_output_get_name(void*) {
    return obs_module_text("OutputName");
}

static void* radio_output_create(obs_data_t* settings, obs_output_t* output) {
    (void)settings;
    radio_output_data* data = new radio_output_data();
    data->output = output;
    data->streamer = new RadioStreamer();
    data->lame = nullptr;
    return data;
}

static void radio_output_destroy(void* data) {
    radio_output_data* ctx = static_cast<radio_output_data*>(data);
    delete ctx->streamer;
    delete ctx;
}

static void radio_output_update(void* data, obs_data_t* settings) {
    radio_output_data* ctx = static_cast<radio_output_data*>(data);
    
    ctx->host = obs_data_get_string(settings, "server_url");
    ctx->port = static_cast<int>(obs_data_get_int(settings, "server_port"));
    ctx->mount = obs_data_get_string(settings, "mountpoint");
    ctx->user = obs_data_get_string(settings, "username");
    if (ctx->user.empty()) ctx->user = "source";
    ctx->pass = obs_data_get_string(settings, "password");
    ctx->bitrate = static_cast<int>(obs_data_get_int(settings, "bitrate"));
    ctx->record_locally = obs_data_get_bool(settings, "record_locally");
    ctx->record_path = obs_data_get_string(settings, "record_path");
    
    if (ctx->port == 0) ctx->port = 8000;
    if (ctx->bitrate == 0) ctx->bitrate = 128;
}

static bool radio_output_start(void* data) {
    radio_output_data* ctx = static_cast<radio_output_data*>(data);
    
    // Validate output structure early on
    if (!obs_output_can_begin_data_capture(ctx->output, 0)) {
        blog(LOG_ERROR, "%s", obs_module_text("ErrorCapture"));
        return false;
    }

    ctx->lame = lame_init();
    if (!ctx->lame) {
        blog(LOG_ERROR, "%s", obs_module_text("ErrorLameInit"));
        return false;
    }

    // Capture main default audio (already initialized by OBS)
    audio_t* audio = obs_output_audio(ctx->output);
    if (!audio) {
        blog(LOG_ERROR, "%s", obs_module_text("ErrorNoAudioContext"));
        lame_close(ctx->lame);
        ctx->lame = nullptr;
        return false;
    }
    
    size_t sample_rate = audio_output_get_sample_rate(audio);
    size_t channels = audio_output_get_channels(audio);

    lame_set_in_samplerate(ctx->lame, static_cast<int>(sample_rate));
    lame_set_num_channels(ctx->lame, static_cast<int>(channels));
    lame_set_brate(ctx->lame, ctx->bitrate);
    lame_set_quality(ctx->lame, 2); // 2 is high quality, 0 is best
    lame_init_params(ctx->lame);

    ctx->streamer->on_disconnect_callback = [output = ctx->output]() {
        std::thread([output]() {
            obs_output_signal_stop(output, OBS_OUTPUT_ERROR);
        }).detach();
    };

    if (!ctx->streamer->connect(ctx->host, ctx->port, ctx->mount, ctx->user, ctx->pass, ctx->bitrate, ctx->record_locally, ctx->record_path)) {
        lame_close(ctx->lame);
        ctx->lame = nullptr;
        return false;
    }
    
    struct audio_convert_info aci = {0};
    aci.format = AUDIO_FORMAT_FLOAT_PLANAR;
    
    obs_output_set_audio_conversion(ctx->output, &aci);
    
    if (!obs_output_begin_data_capture(ctx->output, 0)) {
        ctx->streamer->disconnect();
        lame_close(ctx->lame);
        ctx->lame = nullptr;
        return false;
    }

    blog(LOG_INFO, "%s", obs_module_text("LogStreamStarted"));
    return true;
}

static void radio_output_stop(void* data, uint64_t ts) {
    (void)ts;
    auto* ctx = static_cast<radio_output_data*>(data);
    
    obs_output_end_data_capture(ctx->output);
    
    if (ctx->lame) {
        unsigned char mp3_buffer[8192];
        int bytes = lame_encode_flush(ctx->lame, mp3_buffer, sizeof(mp3_buffer));
        if (bytes > 0 && ctx->streamer) {
            ctx->streamer->push_audio(mp3_buffer, bytes);
        }
        lame_close(ctx->lame);
        ctx->lame = nullptr;
    }

    ctx->streamer->disconnect();
    blog(LOG_INFO, "%s", obs_module_text("LogStreamStopped"));
}

static void radio_output_raw_audio(void* data, struct audio_data* frames) {
    auto* ctx = static_cast<radio_output_data*>(data);
    if (!ctx->lame || !ctx->streamer->is_connected()) return;

    audio_t* audio = obs_output_audio(ctx->output);
    size_t channels = audio_output_get_channels(audio);
    size_t frames_count = frames->frames;
    
    ctx->pcm_buffer.resize(frames_count * channels);

    float** float_data = (float**)frames->data;
    
    for (size_t i = 0; i < frames_count; ++i) {
        for (size_t c = 0; c < channels; ++c) {
            float sample = float_data[c][i];
            
            if (sample > 1.0f) sample = 1.0f;
            else if (sample < -1.0f) sample = -1.0f;
            
            ctx->pcm_buffer[i * channels + c] = static_cast<int16_t>(sample * 32767.0f);
        }
    }

    int mp3_buf_size = static_cast<int>(1.25 * frames_count * channels + 7200);
    std::vector<unsigned char> mp3_buffer_vec(mp3_buf_size);

    int encoded_bytes = lame_encode_buffer_interleaved(
        ctx->lame,
        ctx->pcm_buffer.data(),
        static_cast<int>(frames_count),
        mp3_buffer_vec.data(),
        mp3_buf_size
    );

    if (encoded_bytes > 0) {
        ctx->streamer->push_audio(mp3_buffer_vec.data(), encoded_bytes);
    }
}

static struct obs_output_info create_output_info() {
    struct obs_output_info info = {};
    info.id = "radio_output";
    info.flags = OBS_OUTPUT_AUDIO;
    info.get_name = radio_output_get_name;
    info.create = radio_output_create;
    info.destroy = radio_output_destroy;
    info.update = radio_output_update;
    info.start = radio_output_start;
    info.stop = radio_output_stop;
    info.raw_audio = radio_output_raw_audio;
    return info;
}

struct obs_output_info radio_output_info = create_output_info();
