/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_media.h"

#include "iptv_audio_frame.h"
#include "iptv_video_sps.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
}

#include <atomic>
#include <cerrno>
#include <time.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

extern "C" int sceKernelUsleep(std::uint32_t microseconds);
extern "C" int scePthreadCreate(void **thread, const void *attributes, void *(*entry)(void *),
                                void *argument, const char *name);
extern "C" int scePthreadJoin(void *thread, void **result);

namespace iptv::media
{
namespace
{

constexpr int kIoBufferBytes = 256 * 1024;
// A forward jump this far past the buffered data waits for the download instead of
// restarting it.
constexpr std::int64_t kSkipForwardBytes = 2 * 1024 * 1024;
constexpr std::uint64_t kStallTimeoutUs = UINT64_C(15000000);

std::uint64_t MonotonicUs()
{
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return static_cast<std::uint64_t>(now.tv_sec) * UINT64_C(1000000) +
           static_cast<std::uint64_t>(now.tv_nsec) / UINT64_C(1000);
}

void SetError(char *error, std::size_t capacity, const char *format, ...)
{
    if (!error || !capacity)
        return;
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(error, capacity, format, arguments);
    va_end(arguments);
}

// The file over HTTP, as FFmpeg's I/O layer sees it: a byte position that reads advance and
// seeks move. A download thread keeps one connection reading into a ring buffer at full speed,
// whatever the demuxer is doing; otherwise the connection idles whenever the decoder's queue
// is full, and a high-bitrate scene arrives faster than a slowed connection can recover.
// FFmpeg reads and seeks on the player thread; the download thread only ever fills the ring.
// A seek outside the buffered data asks the thread to restart the download at the new offset,
// so only one connection is ever open (providers often limit an account to one or two).
class HttpIo
{
  public:
    HttpIo(const Source &source, const Sink &sink) : source_(source), sink_(sink)
    {
        request_ = source.request;
        if (request_ && request_->range_start == 0 && request_->total_length >= 0)
            size_.store(request_->total_length, std::memory_order_relaxed);
        std::snprintf(url_, sizeof(url_), "%s",
                      request_ && request_->effective_url[0] ? request_->effective_url
                      : source.url                           ? source.url
                                                             : "");
        ring_ = new (std::nothrow) std::uint8_t[kRingBytes];
        if (!ring_)
            return;
        // The bytes the player already read to recognise the file start the ring.
        std::size_t prefix = source.prefix ? source.prefix_bytes : 0;
        if (prefix > kRingBytes)
            prefix = 0;
        if (prefix)
            std::memcpy(ring_, source.prefix, prefix);
        write_pos_.store(static_cast<std::int64_t>(prefix), std::memory_order_relaxed);
        if (scePthreadCreate(&thread_, nullptr, &HttpIo::ThreadEntry, this,
                             "prosperotv-media-io") != 0)
            thread_ = nullptr;
    }

    ~HttpIo()
    {
        quit_.store(true, std::memory_order_release);
        if (thread_)
            scePthreadJoin(thread_, nullptr);
        iptv::http::CloseStream(&owned_);
        delete[] ring_;
    }

    bool ready() const
    {
        return ring_ && thread_;
    }
    std::int64_t size() const
    {
        return size_.load(std::memory_order_acquire);
    }
    bool Stopped()
    {
        return sink_.stop && sink_.stop(sink_.context);
    }

    int Read(std::uint8_t *buffer, int capacity)
    {
        if (capacity <= 0)
            return 0;
        for (unsigned waited = 0;; ++waited)
        {
            // Polling the controller is cheap, but not every millisecond.
            if (waited % 20u == 0 && Stopped())
                return AVERROR_EXIT;
            const std::int64_t read = read_pos_.load(std::memory_order_relaxed);
            const std::int64_t write = write_pos_.load(std::memory_order_acquire);
            if (write > read)
            {
                const std::size_t offset = static_cast<std::size_t>(read % kRingBytes);
                std::size_t count = static_cast<std::size_t>(write - read);
                if (count > static_cast<std::size_t>(capacity))
                    count = static_cast<std::size_t>(capacity);
                if (count > kRingBytes - offset)
                    count = kRingBytes - offset;
                std::memcpy(buffer, ring_ + offset, count);
                read_pos_.store(read + static_cast<std::int64_t>(count), std::memory_order_release);
                return static_cast<int>(count);
            }
            const State state = state_.load(std::memory_order_acquire);
            if (state == State::ended)
                return AVERROR_EOF;
            if (state != State::running)
                return AVERROR(EIO);
            sceKernelUsleep(1000u);
        }
    }

    std::int64_t Seek(std::int64_t offset, int whence)
    {
        const std::int64_t size = this->size();
        if (whence & AVSEEK_SIZE)
            return size >= 0 ? size : AVERROR(ENOSYS);
        whence &= ~AVSEEK_FORCE;
        const std::int64_t read = read_pos_.load(std::memory_order_relaxed);
        std::int64_t target = -1;
        if (whence == SEEK_SET)
            target = offset;
        else if (whence == SEEK_CUR)
            target = read + offset;
        else if (whence == SEEK_END && size >= 0)
            target = size + offset;
        if (target < 0)
            return AVERROR(EINVAL);
        if (target == read)
            return read;

        const std::int64_t write = write_pos_.load(std::memory_order_acquire);
        const std::int64_t start = run_start_.load(std::memory_order_acquire);
        // Behind: still in the ring if the thread has not overwritten it (with a margin for
        // the chunk it may be writing now).
        const std::int64_t oldest =
            write - static_cast<std::int64_t>(kRingBytes) + static_cast<std::int64_t>(kChunkBytes);
        if (target < read && target >= start && target >= oldest)
        {
            read_pos_.store(target, std::memory_order_release);
            return target;
        }
        // Ahead within what is buffered or about to be: skip to it.
        if (target > read && target - write <= kSkipForwardBytes &&
            state_.load(std::memory_order_acquire) == State::running)
        {
            for (unsigned waited = 0;; ++waited)
            {
                const std::int64_t available = write_pos_.load(std::memory_order_acquire);
                read_pos_.store(available < target ? available : target, std::memory_order_release);
                if (available >= target)
                    return target;
                if (state_.load(std::memory_order_acquire) != State::running)
                    break;
                if (waited % 20u == 0 && Stopped())
                    return AVERROR_EXIT;
                sceKernelUsleep(1000u);
            }
        }
        if (!seekable_.load(std::memory_order_acquire))
            return AVERROR(ESPIPE);
        // Anywhere else: the download restarts there.
        restart_target_.store(target, std::memory_order_relaxed);
        const std::uint32_t request = restart_request_.load(std::memory_order_relaxed) + 1u;
        restart_request_.store(request, std::memory_order_release);
        for (unsigned waited = 0; restart_done_.load(std::memory_order_acquire) != request;
             ++waited)
        {
            if (waited % 20u == 0 && Stopped())
                return AVERROR_EXIT;
            sceKernelUsleep(1000u);
        }
        if (!seekable_.load(std::memory_order_acquire))
            return AVERROR(ESPIPE);
        if (state_.load(std::memory_order_acquire) == State::failed)
            return AVERROR(EIO);
        return target;
    }

  private:
    enum class State : std::uint8_t
    {
        running,
        ended,  // the whole file (or, without a known size, the whole response) is buffered
        failed, // the connection could not be restored
    };

    static constexpr std::size_t kRingBytes = 24u * 1024u * 1024u;
    static constexpr std::size_t kChunkBytes = 256u * 1024u;
    static constexpr unsigned kReconnectAttempts = 6;

    static void *ThreadEntry(void *context)
    {
        static_cast<HttpIo *>(context)->Download();
        return nullptr;
    }

    bool Quitting() const
    {
        return quit_.load(std::memory_order_acquire);
    }

    // Sleeps in short steps, ending early for a quit or a restart request.
    bool Pause(std::uint64_t microseconds, std::uint32_t handled)
    {
        for (std::uint64_t waited = 0; waited < microseconds; waited += 10000u)
        {
            if (Quitting() || restart_request_.load(std::memory_order_acquire) != handled)
                return false;
            sceKernelUsleep(10000u);
        }
        return true;
    }

    void Download()
    {
        std::uint32_t handled = 0;
        unsigned failures = 0;
        while (!Quitting())
        {
            const std::uint32_t request = restart_request_.load(std::memory_order_acquire);
            if (request != handled)
            {
                const std::int64_t target = restart_target_.load(std::memory_order_relaxed);
                if (request_)
                    iptv::http::CloseStream(request_);
                write_pos_.store(target, std::memory_order_release);
                read_pos_.store(target, std::memory_order_release);
                run_start_.store(target, std::memory_order_release);
                state_.store(Reopen(target) ? State::running : State::failed,
                             std::memory_order_release);
                failures = 0;
                handled = request;
                restart_done_.store(request, std::memory_order_release);
                continue;
            }
            if (state_.load(std::memory_order_acquire) != State::running)
            {
                (void)Pause(2000u, handled);
                continue;
            }
            const std::int64_t write = write_pos_.load(std::memory_order_relaxed);
            const std::int64_t size = this->size();
            if (size >= 0 && write >= size)
            {
                state_.store(State::ended, std::memory_order_release);
                continue;
            }
            const std::int64_t used = write - read_pos_.load(std::memory_order_acquire);
            const std::size_t free = kRingBytes - static_cast<std::size_t>(used);
            if (free < kChunkBytes)
            {
                (void)Pause(1000u, handled);
                continue;
            }
            if (!request_ || !request_->open)
            {
                // Reconnect where the data stopped, backing off up to about 8 seconds.
                if (failures >= kReconnectAttempts)
                {
                    state_.store(State::failed, std::memory_order_release);
                    continue;
                }
                if (failures && !Pause(UINT64_C(250000) << (failures - 1u), handled))
                    continue;
                ++failures;
                if (!Reopen(write) && !seekable_.load(std::memory_order_acquire))
                    state_.store(State::failed, std::memory_order_release);
                continue;
            }
            const std::size_t offset = static_cast<std::size_t>(write % kRingBytes);
            std::size_t chunk = kChunkBytes;
            if (chunk > kRingBytes - offset)
                chunk = kRingBytes - offset;
            const int received = iptv::http::ReadStream(request_, ring_ + offset, chunk);
            if (received > 0)
            {
                failures = 0;
                write_pos_.store(write + received, std::memory_order_release);
            }
            else if (received == 0 && size < 0)
            {
                state_.store(State::ended, std::memory_order_release);
            }
            else
            {
                // An error, or the body ended before the file did: reconnect from here.
                iptv::http::CloseStream(request_);
            }
        }
        if (request_)
            iptv::http::CloseStream(request_);
    }

    // Opens the file again from `offset`. A server that answers a ranged request with the
    // whole file cannot seek, and the file is then read only from the start.
    bool Reopen(std::int64_t offset)
    {
        if (request_)
            iptv::http::CloseStream(request_);
        const char *urls[2] = {url_, source_.url};
        for (const char *url : urls)
        {
            if (!url || !*url || Quitting())
                continue;
            iptv::http::CloseStream(&owned_);
            const auto status = iptv::http::OpenStream(url, source_.accept, &owned_,
                                                       source_.headers, offset > 0 ? offset : -1);
            if (status != iptv::http::Status::ok)
                continue;
            if (offset > 0 && owned_.range_start != offset)
            {
                iptv::http::CloseStream(&owned_);
                seekable_.store(false, std::memory_order_release);
                return false;
            }
            request_ = &owned_;
            if (size() < 0 && owned_.total_length >= 0)
                size_.store(owned_.total_length, std::memory_order_release);
            return true;
        }
        return false;
    }

    const Source &source_;
    const Sink &sink_;
    iptv::http::StreamRequest *request_ = nullptr; // used by the download thread only
    iptv::http::StreamRequest owned_{};
    char url_[iptv::http::kMaxUrlBytes + 1u] = {};
    std::uint8_t *ring_ = nullptr;
    void *thread_ = nullptr;
    std::atomic<std::int64_t> read_pos_{0};  // advanced by the player thread
    std::atomic<std::int64_t> write_pos_{0}; // advanced by the download thread
    std::atomic<std::int64_t> run_start_{0}; // first offset of the current download
    std::atomic<std::int64_t> size_{-1};
    std::atomic<State> state_{State::running};
    std::atomic<bool> seekable_{true};
    std::atomic<bool> quit_{false};
    std::atomic<std::int64_t> restart_target_{0};
    std::atomic<std::uint32_t> restart_request_{0};
    std::atomic<std::uint32_t> restart_done_{0};
};

int ReadPacket(void *opaque, std::uint8_t *buffer, int capacity)
{
    return static_cast<HttpIo *>(opaque)->Read(buffer, capacity);
}

std::int64_t SeekPacket(void *opaque, std::int64_t offset, int whence)
{
    return static_cast<HttpIo *>(opaque)->Seek(offset, whence);
}

int Interrupted(void *opaque)
{
    return static_cast<HttpIo *>(opaque)->Stopped() ? 1 : 0;
}

// The MPEG-TS stream type the native audio path expects for a codec; 0 when unsupported.
std::uint32_t AudioStreamType(const AVCodecParameters *codec, AacConfig *aac)
{
    switch (codec->codec_id)
    {
    case AV_CODEC_ID_AAC:
        return ParseAudioSpecificConfig(codec->extradata,
                                        static_cast<std::size_t>(codec->extradata_size), aac)
                   ? 0x0fu
                   : 0u;
    case AV_CODEC_ID_AC3:
        return 0x81u;
    case AV_CODEC_ID_EAC3:
        return 0x87u;
    case AV_CODEC_ID_MP2:
        return 0x03u;
    default:
        return 0u;
    }
}

struct Player
{
    const Sink &sink;
    char *error;
    std::size_t error_capacity;
    AVFormatContext *format = nullptr;
    AVStream *video = nullptr;
    AVStream *audio = nullptr;
    AVBSFContext *video_filter = nullptr;
    AVBSFContext *audio_filter = nullptr; // E-AC-3: keep the independent substream
    AacConfig aac{};
    std::uint32_t audio_type = 0;
    std::uint32_t codec = IPTV_STREAM_VIDEO_UNKNOWN;
    bool opened = false;
    std::int64_t base_us = 0;
    std::uint64_t last_video_us = 0;
    std::vector<std::uint8_t> frame;
    std::vector<AVStream *> audio_streams; // the audio tracks this player can play
    // Video starts (and restarts after a seek) at a random-access picture, as the MPEG-TS
    // path does; HEVC RASL pictures after a CRA there refer to pictures that were never
    // decoded and are skipped. Audio waits for the first picture so the two start together.
    bool random_access_seen = false;
    bool skip_rasl = false;
    unsigned waiting_for_random_access = 0;
    bool audio_hold = false;
    std::uint64_t audio_floor_us = 0;

    std::uint64_t PtsUs(const AVPacket *packet, const AVStream *stream) const
    {
        const std::int64_t stamp = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
        if (stamp == AV_NOPTS_VALUE)
            return IPTV_STREAM_PTS_UNKNOWN;
        return NormalizePtsUs(av_rescale_q(stamp, stream->time_base, AVRational{1, 1000000}),
                              base_us);
    }

    int OpenDecoder(const AVPacket *packet)
    {
        const std::uint8_t *nal = nullptr;
        std::size_t nal_bytes = 0;
        video::SpsInfo sps;
        const char *problem = nullptr;
        if (!video::FindSps(packet->data, static_cast<std::size_t>(packet->size), codec, &nal,
                            &nal_bytes))
        {
            SetError(error, error_capacity, "The video has no sequence header to start from.");
            return -1;
        }
        const int parsed = codec == IPTV_STREAM_VIDEO_H264
                               ? video::ParseH264Sps(nal, nal_bytes, &sps, &problem)
                               : video::ParseHevcSps(nal, nal_bytes, &sps, &problem);
        if (parsed != IPTV_STREAM_OK)
        {
            SetError(error, error_capacity, "This video cannot be decoded by the console (%s).",
                     problem ? problem : "unsupported format");
            return -1;
        }
        if (codec == IPTV_STREAM_VIDEO_H264 && sps.profile != 66u && sps.profile != 77u &&
            sps.profile != 100u)
        {
            SetError(error, error_capacity,
                     "This video uses an H.264 profile (%u) the console cannot decode.",
                     static_cast<unsigned>(sps.profile));
            return -1;
        }
        iptv_stream_format_t stream{};
        stream.video_codec = codec;
        stream.video_stream_type = codec == IPTV_STREAM_VIDEO_H264 ? 0x1bu : 0x24u;
        stream.video_profile = sps.profile;
        stream.video_level = sps.level;
        stream.coded_width = sps.coded_width;
        stream.coded_height = sps.coded_height;
        stream.visible_width = sps.visible_width;
        stream.visible_height = sps.visible_height;
        stream.video_bit_depth = sps.bit_depth;
        stream.video_chroma_format = sps.chroma;
        if (audio && audio_type)
        {
            stream.audio_pid = static_cast<std::uint32_t>(audio->index) + 1u;
            stream.audio_stream_type = audio_type;
            stream.audio_sample_rate = static_cast<std::uint32_t>(audio->codecpar->sample_rate);
            stream.audio_channels =
                static_cast<std::uint32_t>(audio->codecpar->ch_layout.nb_channels);
        }
        if (sink.open(sink.context, &stream) != 0)
        {
            SetError(error, error_capacity,
                     "The video decoder could not open this %s video (%ux%u, profile %u, "
                     "level %u).",
                     codec == IPTV_STREAM_VIDEO_H264 ? "H.264" : "HEVC",
                     static_cast<unsigned>(sps.visible_width),
                     static_cast<unsigned>(sps.visible_height), static_cast<unsigned>(sps.profile),
                     static_cast<unsigned>(sps.level));
            return -1;
        }
        opened = true;
        return 0;
    }

    int SubmitVideo(const AVPacket *packet)
    {
        const bool hevc = codec == IPTV_STREAM_VIDEO_HEVC;
        const int type =
            video::PictureType(packet->data, static_cast<std::size_t>(packet->size), codec);
        const bool random_access = hevc ? type >= 16 && type <= 21 : type == 5;
        if (!random_access_seen)
        {
            // H.264 files that mark recovery points instead of IDR pictures start at a
            // keyframe once waiting has gone on for a while.
            const bool fallback =
                !hevc && (packet->flags & AV_PKT_FLAG_KEY) && waiting_for_random_access >= 300u;
            if (!random_access && !fallback)
            {
                ++waiting_for_random_access;
                return 0;
            }
            random_access_seen = true;
            skip_rasl = hevc;
            waiting_for_random_access = 0;
        }
        else if (hevc && random_access)
        {
            // A later CRA has its references; an IDR or BLA starts a new sequence.
            skip_rasl = type != 21;
        }
        if (hevc && skip_rasl && (type == 8 || type == 9))
            return 0;
        // The first picture carries the parameter sets the decoder opens with.
        if (!opened && OpenDecoder(packet) != 0)
            return -1;
        std::uint64_t pts = PtsUs(packet, video);
        if (pts == IPTV_STREAM_PTS_UNKNOWN)
            pts = last_video_us;
        last_video_us = pts;
        if (audio_hold)
        {
            audio_hold = false;
            audio_floor_us = pts > UINT64_C(100000) ? pts - UINT64_C(100000) : 0;
        }
        if (sink.video(sink.context, packet->data, static_cast<std::size_t>(packet->size), pts) !=
            0)
        {
            SetError(error, error_capacity, "The video decoder refused a frame of this file.");
            return -1;
        }
        return 0;
    }

    void SubmitAudio(const AVPacket *packet)
    {
        if (!opened || !audio_type || !packet->size || audio_hold)
            return;
        std::uint64_t pts = PtsUs(packet, audio);
        if (pts != IPTV_STREAM_PTS_UNKNOWN && pts < audio_floor_us)
            return;
        const std::uint8_t *data = packet->data;
        std::size_t bytes = static_cast<std::size_t>(packet->size);
        if (audio_type == 0x0fu)
        {
            // Raw AAC gets an ADTS header; a packet that already has one is sent as it is.
            if (bytes >= 2u && data[0] == 0xffu && (data[1] & 0xf0u) == 0xf0u)
            {
                (void)sink.audio(sink.context, data, bytes, pts);
                return;
            }
            frame.resize(kAdtsHeaderBytes + bytes);
            if (!WriteAdtsHeader(aac, bytes, frame.data()))
                return;
            std::memcpy(frame.data() + kAdtsHeaderBytes, data, bytes);
            (void)sink.audio(sink.context, frame.data(), frame.size(), pts);
            return;
        }
        if (audio_type == 0x03u)
        {
            (void)sink.audio(sink.context, data, bytes, pts);
            return;
        }
        // AC-3 and E-AC-3: one sync frame per call, as the console's decoder takes them.
        while (bytes)
        {
            std::uint32_t rate = 0;
            std::uint32_t samples = 0;
            const std::size_t size =
                iptv_audio_frame_info(audio_type, data, bytes, &rate, &samples);
            if (!size || size > bytes)
                return;
            (void)sink.audio(sink.context, data, size, pts);
            if (pts != IPTV_STREAM_PTS_UNKNOWN && rate)
                pts += static_cast<std::uint64_t>(samples) * UINT64_C(1000000) / rate;
            data += size;
            bytes -= size;
        }
    }

    // Sends a packet through a stream's filter (or straight on), then submits what comes out.
    int Feed(AVBSFContext *filter, AVPacket *packet, AVPacket *filtered, bool is_video)
    {
        if (!filter)
        {
            if (is_video)
                return SubmitVideo(packet);
            SubmitAudio(packet);
            return 0;
        }
        if (av_bsf_send_packet(filter, packet) < 0)
            return is_video ? -1 : 0;
        for (;;)
        {
            const int received = av_bsf_receive_packet(filter, filtered);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF)
                return 0;
            if (received < 0)
            {
                if (is_video)
                    SetError(error, error_capacity, "The video track could not be read.");
                return is_video ? -1 : 0;
            }
            int result = 0;
            if (is_video)
                result = SubmitVideo(filtered);
            else
                SubmitAudio(filtered);
            av_packet_unref(filtered);
            if (result != 0)
                return result;
        }
    }
};

AVBSFContext *CreateFilter(const char *name, const AVStream *stream)
{
    const AVBitStreamFilter *filter = av_bsf_get_by_name(name);
    AVBSFContext *context = nullptr;
    if (!filter || av_bsf_alloc(filter, &context) < 0)
        return nullptr;
    if (avcodec_parameters_copy(context->par_in, stream->codecpar) < 0)
    {
        av_bsf_free(&context);
        return nullptr;
    }
    context->time_base_in = stream->time_base;
    if (av_bsf_init(context) < 0)
        av_bsf_free(&context);
    return context;
}

// "English  AC-3 5.1": the track's title or language, its codec and its channels.
void AudioLabel(const AVStream *stream, unsigned number, char *out, std::size_t capacity)
{
    const AVDictionaryEntry *title = av_dict_get(stream->metadata, "title", nullptr, 0);
    const AVDictionaryEntry *language = av_dict_get(stream->metadata, "language", nullptr, 0);
    char name[40] = {};
    if (title && title->value[0])
        std::snprintf(name, sizeof(name), "%.39s", title->value);
    else if (language && language->value[0] && std::strcmp(language->value, "und") != 0)
    {
        std::snprintf(name, sizeof(name), "%.39s", language->value);
        for (char &character : name)
            if (character >= 'a' && character <= 'z')
                character = static_cast<char>(character - 'a' + 'A');
    }
    else
        std::snprintf(name, sizeof(name), "Track %u", number);
    const AVCodecParameters *codec = stream->codecpar;
    const char *format = codec->codec_id == AV_CODEC_ID_AAC    ? "AAC"
                         : codec->codec_id == AV_CODEC_ID_AC3  ? "AC-3"
                         : codec->codec_id == AV_CODEC_ID_EAC3 ? "E-AC-3"
                                                               : "MP2";
    const int channels = codec->ch_layout.nb_channels;
    const char *layout = channels == 1   ? "Mono"
                         : channels == 2 ? "Stereo"
                         : channels == 6 ? "5.1"
                         : channels == 8 ? "7.1"
                                         : "";
    std::snprintf(out, capacity, "%s  %s %s", name, format, layout);
}

} // namespace

int Play(const Source &source, const Sink &sink, char *error, std::size_t error_capacity)
{
    if (error && error_capacity)
        error[0] = '\0';
    if (!source.request || !sink.open || !sink.video || !sink.audio || !sink.stop ||
        (source.container != Container::matroska && source.container != Container::mp4))
    {
        SetError(error, error_capacity, "This file type cannot be played.");
        return -1;
    }
#if IPTV_PROBE
    av_log_set_level(AV_LOG_ERROR);
#else
    av_log_set_level(AV_LOG_QUIET);
#endif

    HttpIo io(source, sink);
    if (!io.ready())
    {
        SetError(error, error_capacity, "Not enough memory to buffer this file.");
        return -1;
    }
    auto *buffer = static_cast<std::uint8_t *>(av_malloc(kIoBufferBytes));
    AVIOContext *context =
        buffer ? avio_alloc_context(buffer, kIoBufferBytes, 0, &io, ReadPacket, nullptr, SeekPacket)
               : nullptr;
    AVFormatContext *format = context ? avformat_alloc_context() : nullptr;
    if (!format)
    {
        if (context)
            av_freep(&context->buffer);
        else
            av_free(buffer);
        avio_context_free(&context);
        SetError(error, error_capacity, "Not enough memory to open this file.");
        return -1;
    }
    context->seekable = io.size() >= 0 ? AVIO_SEEKABLE_NORMAL : 0;
    format->pb = context;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    format->interrupt_callback = {Interrupted, &io};

    Player player{sink, error, error_capacity};
    int result = 0;
    AVPacket *packet = av_packet_alloc();
    AVPacket *filtered = av_packet_alloc();
    const AVInputFormat *input =
        av_find_input_format(source.container == Container::mp4 ? "mov" : "matroska");
    // The container's header gives the codecs, sizes and setup data, so FFmpeg's stream probe
    // (which would download and decode a few seconds first) is skipped.
    int opened = packet && filtered && input ? avformat_open_input(&format, nullptr, input, nullptr)
                                             : AVERROR(ENOMEM);
    if (opened < 0)
    {
        if (opened == AVERROR_EXIT)
        {
            result = 1;
        }
        else
        {
            char reason[96] = {};
            av_strerror(opened, reason, sizeof(reason));
            SetError(error, error_capacity, "The file could not be opened (%s).", reason);
            result = -1;
        }
        format = nullptr; // freed by avformat_open_input
    }

    if (result == 0)
    {
        player.format = format;
        const AVStream *other_video = nullptr;
        bool default_audio = false;
        for (unsigned index = 0; index < format->nb_streams; ++index)
        {
            AVStream *stream = format->streams[index];
            const AVCodecParameters *codec = stream->codecpar;
            stream->discard = AVDISCARD_ALL;
            if (codec->codec_type == AVMEDIA_TYPE_VIDEO &&
                !(stream->disposition & AV_DISPOSITION_ATTACHED_PIC))
            {
                if (!player.video &&
                    (codec->codec_id == AV_CODEC_ID_H264 || codec->codec_id == AV_CODEC_ID_HEVC))
                    player.video = stream;
                else if (!other_video)
                    other_video = stream;
            }
            else if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                AacConfig aac;
                const std::uint32_t type = AudioStreamType(codec, &aac);
                if (type)
                    player.audio_streams.push_back(stream);
                const bool is_default = (stream->disposition & AV_DISPOSITION_DEFAULT) != 0;
                if (type && (!player.audio || (is_default && !default_audio)))
                {
                    player.audio = stream;
                    player.audio_type = type;
                    player.aac = aac;
                    default_audio = is_default;
                }
            }
        }
        if (!player.video)
        {
            SetError(error, error_capacity,
                     other_video ? "This video uses %s, which the console cannot decode."
                                 : "This file has no video track.",
                     other_video ? avcodec_get_name(other_video->codecpar->codec_id) : "");
            result = -1;
        }
    }

    if (result == 0)
    {
        player.video->discard = AVDISCARD_DEFAULT;
        player.codec = player.video->codecpar->codec_id == AV_CODEC_ID_H264
                           ? IPTV_STREAM_VIDEO_H264
                           : IPTV_STREAM_VIDEO_HEVC;
        player.video_filter = CreateFilter(
            player.codec == IPTV_STREAM_VIDEO_H264 ? "h264_mp4toannexb" : "hevc_mp4toannexb",
            player.video);
        if (!player.video_filter)
        {
            SetError(error, error_capacity, "The video track's setup data could not be read.");
            result = -1;
        }
        if (player.audio)
        {
            player.audio->discard = AVDISCARD_DEFAULT;
            if (player.audio_type == 0x87u)
                player.audio_filter = CreateFilter("eac3_core", player.audio);
        }
        player.base_us = format->start_time != AV_NOPTS_VALUE ? format->start_time : 0;
    }

    // Playback controls. A seek waits briefly for further presses, so holding a direction
    // moves one target along the timeline before anything is downloaded.
    const std::int64_t duration_us =
        result == 0 && format->duration != AV_NOPTS_VALUE && format->duration > 0 ? format->duration
                                                                                  : -1;
    bool paused = false;
    std::int64_t pending_seek_us = -1;
    std::uint64_t pending_seek_at = 0;
    std::int64_t seek_target_us = -1; // shown until a picture of the new position appears
    std::uint32_t seek_generation = 0;
    std::uint64_t last_status_at = 0;
    Status status;

    const auto now_position = [&]() -> std::int64_t
    {
        if (pending_seek_us >= 0)
            return pending_seek_us;
        if (!player.opened || !sink.position)
            return seek_target_us >= 0 ? seek_target_us : source.start_position_us;
        std::uint32_t generation = 0;
        const std::uint64_t pts = sink.position(sink.context, &generation);
        if (seek_target_us >= 0 && generation != seek_generation)
            return seek_target_us;
        seek_target_us = -1;
        return static_cast<std::int64_t>(pts);
    };
    const auto report = [&](bool force)
    {
        const std::uint64_t now = MonotonicUs();
        if (!sink.status || (!force && now - last_status_at < UINT64_C(200000)))
            return;
        last_status_at = now;
        status.position_us = now_position();
        status.duration_us = duration_us;
        status.paused = paused;
        status.seeking = pending_seek_us >= 0 || seek_target_us >= 0;
        status.audio_tracks = static_cast<unsigned>(player.audio_streams.size());
        status.audio_track = 0;
        status.audio_label[0] = '\0';
        for (unsigned index = 0; index < player.audio_streams.size(); ++index)
            if (player.audio_streams[index] == player.audio)
            {
                status.audio_track = index + 1u;
                AudioLabel(player.audio, index + 1u, status.audio_label,
                           sizeof(status.audio_label));
            }
        sink.status(sink.context, &status);
    };
    // Moves playback to `target_us`; a non-zero audio type also switches the decoder's audio.
    const auto seek = [&](std::int64_t target_us, std::uint32_t audio_type) -> bool
    {
        if (target_us < 0)
            target_us = 0;
        if (duration_us > 0 && target_us > duration_us - INT64_C(3000000))
            target_us = duration_us > INT64_C(3000000) ? duration_us - INT64_C(3000000) : 0;
        const std::int64_t stamp = target_us + player.base_us;
        int sought = avformat_seek_file(format, -1, INT64_MIN, stamp, stamp, 0);
        if (sought < 0)
            sought = av_seek_frame(format, -1, stamp, AVSEEK_FLAG_BACKWARD);
        if (sought < 0)
            return false;
        av_bsf_flush(player.video_filter);
        if (player.audio_filter)
            av_bsf_flush(player.audio_filter);
        player.random_access_seen = false;
        player.audio_hold = player.audio != nullptr;
        player.audio_floor_us = 0;
        if (player.opened && sink.seek_reset)
        {
            (void)sink.seek_reset(sink.context, audio_type);
            seek_generation = sink.generation ? sink.generation(sink.context) : 0;
        }
        if (paused && sink.pause)
            (void)sink.pause(sink.context, false);
        paused = false;
        seek_target_us = target_us;
        return true;
    };
    const auto next_audio = [&]()
    {
        const std::size_t count = player.audio_streams.size();
        if (count < 2u || !player.audio)
            return;
        std::size_t index = 0;
        while (index < count && player.audio_streams[index] != player.audio)
            ++index;
        AVStream *next = player.audio_streams[(index + 1u) % count];
        AacConfig aac;
        const std::uint32_t type = AudioStreamType(next->codecpar, &aac);
        if (!type)
            return;
        const std::int64_t position = now_position();
        player.audio->discard = AVDISCARD_ALL;
        next->discard = AVDISCARD_DEFAULT;
        av_bsf_free(&player.audio_filter);
        if (type == 0x87u)
            player.audio_filter = CreateFilter("eac3_core", next);
        player.audio = next;
        player.audio_type = type;
        player.aac = aac;
        // Picking up the new track at the same moment means reading that part again.
        (void)seek(position >= 0 ? position : 0, type);
    };

    if (result == 0 && source.start_position_us > 0)
        (void)seek(source.start_position_us, 0);
    report(true);

    std::uint64_t last_presented = sink.presented ? sink.presented(sink.context) : 0;
    std::uint64_t last_progress = MonotonicUs();
    while (result == 0)
    {
        if (io.Stopped())
        {
            result = 1;
            break;
        }
        for (Command command = sink.command ? sink.command(sink.context) : Command::none;
             command != Command::none;
             command = sink.command ? sink.command(sink.context) : Command::none)
        {
            const std::int64_t step = command == Command::back           ? -INT64_C(10000000)
                                      : command == Command::forward      ? INT64_C(10000000)
                                      : command == Command::back_long    ? -INT64_C(60000000)
                                      : command == Command::forward_long ? INT64_C(60000000)
                                                                         : 0;
            if (step)
            {
                const std::int64_t from = pending_seek_us >= 0 ? pending_seek_us : now_position();
                pending_seek_us = (from > 0 ? from : 0) + step;
                if (pending_seek_us < 0)
                    pending_seek_us = 0;
                if (duration_us > 0 && pending_seek_us > duration_us)
                    pending_seek_us = duration_us;
                pending_seek_at = MonotonicUs() + UINT64_C(400000);
            }
            else if (command == Command::toggle_pause && player.opened && sink.pause)
            {
                paused = !paused;
                (void)sink.pause(sink.context, paused);
            }
            else if (command == Command::next_audio)
            {
                next_audio();
            }
            else if (command == Command::start_over)
            {
                pending_seek_us = -1;
                (void)seek(0, 0);
            }
            report(true);
        }
        if (pending_seek_us >= 0 && MonotonicUs() >= pending_seek_at)
        {
            const std::int64_t target = pending_seek_us;
            pending_seek_us = -1;
            (void)seek(target, 0);
            last_progress = MonotonicUs();
            report(true);
        }
        report(false);
        if (paused)
        {
            // Nothing is read while paused; the download thread fills its buffer meanwhile.
            sceKernelUsleep(20000u);
            last_progress = MonotonicUs();
            continue;
        }

        const int read = av_read_frame(format, packet);
        if (read == AVERROR_EOF)
        {
            // Flush what the filters still hold, then the decoder plays out its queue.
            (void)player.Feed(player.video_filter, nullptr, filtered, true);
            if (player.audio_filter)
                (void)player.Feed(player.audio_filter, nullptr, filtered, false);
            break;
        }
        if (read == AVERROR_EXIT)
        {
            result = 1;
            break;
        }
        if (read < 0)
        {
            char reason[96] = {};
            av_strerror(read, reason, sizeof(reason));
            SetError(error, error_capacity, "Reading the file failed (%s).", reason);
            result = -1;
            break;
        }
        if (player.video && packet->stream_index == player.video->index)
            result = player.Feed(player.video_filter, packet, filtered, true);
        else if (player.audio && packet->stream_index == player.audio->index)
            (void)player.Feed(player.audio_filter, packet, filtered, false);
        av_packet_unref(packet);

        // A decoder that stops presenting frames would otherwise hang playback.
        if (sink.presented && player.opened)
        {
            const std::uint64_t presented = sink.presented(sink.context);
            const std::uint64_t now = MonotonicUs();
            if (presented > last_presented)
            {
                last_presented = presented;
                last_progress = now;
            }
            else if (now - last_progress >= kStallTimeoutUs)
            {
                SetError(error, error_capacity, "The video stopped decoding.");
                result = -1;
            }
        }
    }
    if (result == 0)
        status.position_us = duration_us; // played to the end
    else
        status.position_us = now_position();
    if (sink.status)
        sink.status(sink.context, &status);
    if (result == 0 && !player.opened)
    {
        SetError(error, error_capacity, "The file ended before any video could be decoded.");
        result = -1;
    }

    av_bsf_free(&player.video_filter);
    av_bsf_free(&player.audio_filter);
    av_packet_free(&packet);
    av_packet_free(&filtered);
    avformat_close_input(&format);
    av_freep(&context->buffer);
    avio_context_free(&context);
    return result;
}

} // namespace iptv::media
