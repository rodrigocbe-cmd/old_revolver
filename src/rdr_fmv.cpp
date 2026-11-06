// Videos substitutos (docs/LAYOUT.md, FUNCOES.md #104): um arquivo de video comum em movies/ (.mp4, .mkv, .mov, .webm ou
// .mpg que nao seja PSS) com o nome do video do jogo toca no lugar do original, em qualquer resolucao ou proporcao. O
// player do jogo (FUN_002361B0 inicia, FUN_00236058 atualiza, FUN_00236130 desenha, FUN_00235FF8 encerra, 0x4F7C82 = 1
// no fim) continua sendo chamado pelo jogo: os overrides so desviam inicio, atualizacao e desenho. O FFmpeg decodifica
// numa thread; a janela desenha o quadro por cima da imagem do jogo e toca o som. Sem substituto nada muda.
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "raylib.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    constexpr uint32_t kFmvEnded = 0x004F7C82u;
    constexpr int kAudioRate = 48000;
    constexpr size_t kMaxQueuedSamples = kAudioRate * 2u * 2u; // 2 s estereo

    struct FmvState
    {
        std::thread decoder;
        std::atomic<bool> active{false}, finished{false}, stop{false};
        std::mutex mutex;
        std::vector<uint8_t> frame; // RGBA
        int width = 0, height = 0;
        uint64_t frameSerial = 0u;
        std::deque<int16_t> audio;
    } g_fmv;

    void decodeLoop(std::string path)
    {
        AVFormatContext *format = nullptr;
        AVCodecContext *video = nullptr, *audio = nullptr;
        SwsContext *scaler = nullptr;
        SwrContext *resampler = nullptr;
        AVFrame *decoded = av_frame_alloc();
        AVPacket *packet = av_packet_alloc();
        int videoIndex = -1, audioIndex = -1;
        std::vector<uint8_t> rgba;

        auto openCodec = [&format](int index, AVCodecContext *&ctx) {
            const AVCodec *codec = avcodec_find_decoder(format->streams[index]->codecpar->codec_id);
            ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
            return ctx && avcodec_parameters_to_context(ctx, format->streams[index]->codecpar) >= 0 &&
                   avcodec_open2(ctx, codec, nullptr) >= 0;
        };

        bool ok = avformat_open_input(&format, path.c_str(), nullptr, nullptr) == 0 && avformat_find_stream_info(format, nullptr) >= 0;
        if (ok)
        {
            videoIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            audioIndex = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            ok = videoIndex >= 0 && openCodec(videoIndex, video);
            if (audioIndex >= 0 && !openCodec(audioIndex, audio))
                audioIndex = -1;
        }
        if (ok && audioIndex >= 0)
        {
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            ok = swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_S16, kAudioRate, &audio->ch_layout, audio->sample_fmt,
                                     audio->sample_rate, 0, nullptr) == 0 &&
                 swr_init(resampler) >= 0;
        }
        if (!ok)
            std::fprintf(stderr, "[rdr] video substituto nao abriu: %s\n", path.c_str());

        const auto start = std::chrono::steady_clock::now();
        auto handleVideo = [&]() {
            const double pts = (decoded->best_effort_timestamp == AV_NOPTS_VALUE ? 0.0 : decoded->best_effort_timestamp *
                                                                                        av_q2d(format->streams[videoIndex]->time_base));
            scaler = sws_getCachedContext(scaler, decoded->width, decoded->height, static_cast<AVPixelFormat>(decoded->format),
                                          decoded->width, decoded->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            rgba.resize(static_cast<size_t>(decoded->width) * decoded->height * 4u);
            uint8_t *dst[4] = {rgba.data(), nullptr, nullptr, nullptr};
            int dstLine[4] = {decoded->width * 4, 0, 0, 0};
            sws_scale(scaler, decoded->data, decoded->linesize, 0, decoded->height, dst, dstLine);
            while (!g_fmv.stop.load(std::memory_order_relaxed))
            {
                const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                if (now >= pts)
                    break;
                std::this_thread::sleep_for(std::chrono::duration<double>(std::min(0.01, pts - now)));
            }
            std::lock_guard<std::mutex> lock(g_fmv.mutex);
            g_fmv.frame.swap(rgba);
            g_fmv.width = decoded->width;
            g_fmv.height = decoded->height;
            ++g_fmv.frameSerial;
        };
        auto handleAudio = [&]() {
            const int maxOut = swr_get_out_samples(resampler, decoded->nb_samples);
            std::vector<int16_t> pcm(static_cast<size_t>(std::max(maxOut, 0)) * 2u);
            uint8_t *out = reinterpret_cast<uint8_t *>(pcm.data());
            const int got = swr_convert(resampler, &out, maxOut, const_cast<const uint8_t **>(decoded->extended_data), decoded->nb_samples);
            while (got > 0 && !g_fmv.stop.load(std::memory_order_relaxed))
            {
                {
                    std::lock_guard<std::mutex> lock(g_fmv.mutex);
                    if (g_fmv.audio.size() < kMaxQueuedSamples)
                    {
                        g_fmv.audio.insert(g_fmv.audio.end(), pcm.begin(), pcm.begin() + got * 2);
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        };
        auto drain = [&](AVCodecContext *ctx, auto &&handler) {
            while (avcodec_receive_frame(ctx, decoded) == 0)
                handler();
        };

        while (ok && !g_fmv.stop.load(std::memory_order_relaxed) && av_read_frame(format, packet) >= 0)
        {
            if (packet->stream_index == videoIndex && avcodec_send_packet(video, packet) >= 0)
                drain(video, handleVideo);
            else if (packet->stream_index == audioIndex && avcodec_send_packet(audio, packet) >= 0)
                drain(audio, handleAudio);
            av_packet_unref(packet);
        }
        if (ok)
        {
            avcodec_send_packet(video, nullptr);
            drain(video, handleVideo);
            // termina junto com o som que ainda esta na fila
            for (bool pending = true; pending && !g_fmv.stop.load(std::memory_order_relaxed);)
            {
                {
                    std::lock_guard<std::mutex> lock(g_fmv.mutex);
                    pending = !g_fmv.audio.empty();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
        sws_freeContext(scaler);
        swr_free(&resampler);
        avcodec_free_context(&video);
        avcodec_free_context(&audio);
        avformat_close_input(&format);
        av_packet_free(&packet);
        av_frame_free(&decoded);
        g_fmv.finished.store(true, std::memory_order_release);
    }

    std::string utf8(const fs::path &path)
    {
        const auto text = path.u8string();
        return std::string(reinterpret_cast<const char *>(text.data()), text.size());
    }

    void fmvStop()
    {
        g_fmv.stop.store(true, std::memory_order_relaxed);
        if (g_fmv.decoder.joinable())
            g_fmv.decoder.join();
        g_fmv.active.store(false, std::memory_order_release);
    }

    void fmvStart(const fs::path &file)
    {
        fmvStop();
        {
            std::lock_guard<std::mutex> lock(g_fmv.mutex);
            g_fmv.audio.clear();
            g_fmv.frame.clear();
            g_fmv.width = g_fmv.height = 0;
        }
        g_fmv.stop.store(false, std::memory_order_relaxed);
        g_fmv.finished.store(false, std::memory_order_relaxed);
        g_fmv.active.store(true, std::memory_order_release);
        g_fmv.decoder = std::thread(decodeLoop, utf8(file));
    }

    // PSS (formato do jogo): cabecalho de audio da Sony nos primeiros pacotes
    bool isPss(const fs::path &file)
    {
        std::ifstream in(file, std::ios::binary);
        std::vector<char> head(1u << 16);
        in.read(head.data(), static_cast<std::streamsize>(head.size()));
        const size_t n = static_cast<size_t>(in.gcount());
        return std::search(head.begin(), head.begin() + n, "SShd", "SShd" + 4) != head.begin() + n;
    }

    fs::path findReplacement(const std::string &name)
    {
        const fs::path elfDir = PS2Runtime::getIoPaths().elfDirectory;
        std::string upper = name;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        std::error_code ec;
        for (const fs::path &dir : {elfDir.parent_path() / "movies", elfDir / "movies"})
            for (const std::string &stem : {upper, name})
                for (const char *ext : {".mp4", ".mkv", ".mov", ".webm", ".mpg", ".mpeg"})
                {
                    const fs::path file = dir / (stem + ext);
                    if (fs::exists(file, ec) && !isPss(file))
                        return file;
                }
        return {};
    }

    PS2Runtime::RecompiledFunction g_origStart = nullptr, g_origUpdate = nullptr, g_origDraw = nullptr, g_origEnd = nullptr;
    bool g_hostPlaying = false;

    void fmvStartOverride(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // Lista com um nome so: as sequencias do jogo chamam um video por vez
        if (GPR_U32(ctx, 5) == 1u)
        {
            uint32_t ptr = 0u;
            std::memcpy(&ptr, rdram + (GPR_U32(ctx, 4) & 0x1FFFFFFu), 4);
            const std::string name(reinterpret_cast<const char *>(rdram + (ptr & 0x1FFFFFFu)));
            if (const fs::path file = findReplacement(name); !file.empty())
            {
                std::fprintf(stderr, "[rdr] video substituto: %s\n", utf8(file).c_str());
                fmvStart(file);
                g_hostPlaying = true;
                rdram[kFmvEnded] = 0u;
                ctx->pc = GPR_U32(ctx, 31);
                return;
            }
        }
        g_origStart(rdram, ctx, runtime);
    }

    void fmvUpdateOverride(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!g_hostPlaying)
        {
            g_origUpdate(rdram, ctx, runtime);
            return;
        }
        if (g_fmv.finished.load(std::memory_order_acquire))
            rdram[kFmvEnded] = 1u;
        ctx->pc = GPR_U32(ctx, 31);
    }

    void fmvDrawOverride(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!g_hostPlaying)
        {
            g_origDraw(rdram, ctx, runtime);
            return;
        }
        ctx->pc = GPR_U32(ctx, 31);
    }

    void fmvEndOverride(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (g_hostPlaying)
        {
            g_hostPlaying = false;
            fmvStop();
        }
        g_origEnd(rdram, ctx, runtime);
    }
}

void rdrFmvInstall(PS2Runtime &runtime)
{
    g_origStart = runtime.lookupFunction(0x002361B0u);
    g_origUpdate = runtime.lookupFunction(0x00236058u);
    g_origDraw = runtime.lookupFunction(0x00236130u);
    g_origEnd = runtime.lookupFunction(0x00235FF8u);
    runtime.replaceFunction(0x002361B0u, fmvStartOverride);
    runtime.replaceFunction(0x00236058u, fmvUpdateOverride);
    runtime.replaceFunction(0x00236130u, fmvDrawOverride);
    runtime.replaceFunction(0x00235FF8u, fmvEndOverride);
}

// Thread da janela, a cada quadro: desenha o video por cima de tudo e entrega o som
void rdrFmvOverlay()
{
    static Texture2D s_texture{};
    static AudioStream s_stream{};
    static bool s_streamOpen = false;
    static uint64_t s_serial = 0u;
    if (!g_fmv.active.load(std::memory_order_acquire))
    {
        if (s_streamOpen)
        {
            StopAudioStream(s_stream);
            UnloadAudioStream(s_stream);
            s_streamOpen = false;
        }
        if (s_texture.id != 0)
        {
            UnloadTexture(s_texture);
            s_texture = {};
        }
        return;
    }
    if (!s_streamOpen && IsAudioDeviceReady())
    {
        SetAudioStreamBufferSizeDefault(4096);
        s_stream = LoadAudioStream(kAudioRate, 16, 2);
        PlayAudioStream(s_stream);
        s_streamOpen = true;
    }
    {
        std::lock_guard<std::mutex> lock(g_fmv.mutex);
        if (s_streamOpen && IsAudioStreamProcessed(s_stream))
        {
            std::vector<int16_t> block(4096u * 2u, 0);
            const size_t n = std::min(block.size(), g_fmv.audio.size());
            std::copy_n(g_fmv.audio.begin(), n, block.begin());
            g_fmv.audio.erase(g_fmv.audio.begin(), g_fmv.audio.begin() + n);
            UpdateAudioStream(s_stream, block.data(), 4096);
        }
        if (g_fmv.width > 0 && s_serial != g_fmv.frameSerial)
        {
            s_serial = g_fmv.frameSerial;
            if (s_texture.width != g_fmv.width || s_texture.height != g_fmv.height)
            {
                if (s_texture.id != 0)
                    UnloadTexture(s_texture);
                Image image{g_fmv.frame.data(), g_fmv.width, g_fmv.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                s_texture = LoadTextureFromImage(image);
                SetTextureFilter(s_texture, TEXTURE_FILTER_BILINEAR);
            }
            else
                UpdateTexture(s_texture, g_fmv.frame.data());
        }
    }
    const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), BLACK);
    if (s_texture.id != 0)
    {
        const float scale = std::min(sw / s_texture.width, sh / s_texture.height);
        const float w = s_texture.width * scale, h = s_texture.height * scale;
        DrawTexturePro(s_texture, {0, 0, static_cast<float>(s_texture.width), static_cast<float>(s_texture.height)},
                       {(sw - w) / 2.0f, (sh - h) / 2.0f, w, h}, {0, 0}, 0.0f, WHITE);
    }
}
