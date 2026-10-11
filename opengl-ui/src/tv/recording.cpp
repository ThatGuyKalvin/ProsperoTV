// ProsperoTV - Programme jobs survive a restart; media stays on the selected USB drive.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/recording.hpp"
#include "tv/platform.hpp"
#include "tv/portal.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace ptv
{
ProgrammeJobs::~ProgrammeJobs()
{
    close();
}
bool ProgrammeJobs::close()
{
    if (!db_)
        return true;
    if (sqlite3_close(db_) != SQLITE_OK)
        return false;
    db_ = nullptr;
    return true;
}
bool ProgrammeJobs::open(const std::string &path)
{
    if (db_)
        return true;
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK)
    {
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    sqlite3_limit(db_, SQLITE_LIMIT_LENGTH, 16384);
    const char *sql =
        "CREATE TABLE IF NOT EXISTS jobs(id INTEGER PRIMARY KEY AUTOINCREMENT,source INTEGER NOT "
        "NULL,channel TEXT NOT NULL,title TEXT NOT NULL,start INTEGER NOT NULL,end INTEGER NOT "
        "NULL,drive TEXT NOT NULL,recording INTEGER NOT NULL,state INTEGER NOT NULL DEFAULT "
        "0,result TEXT NOT NULL DEFAULT '');"
        "UPDATE jobs SET state=3,result='Interrupted when the app closed' WHERE state=1;";
    if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK)
        return true;
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
}
std::vector<ProgrammeJob> ProgrammeJobs::list() const
{
    std::vector<ProgrammeJob> rows;
    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "SELECT id,source,channel,title,start,end,drive,recording,state,result "
                           "FROM jobs ORDER BY start,id LIMIT 128",
                           -1, &s, nullptr) != SQLITE_OK)
        return rows;
    while (sqlite3_step(s) == SQLITE_ROW)
    {
        auto text = [&](int c)
        {
            auto p = sqlite3_column_text(s, c);
            return p ? std::string(reinterpret_cast<const char *>(p)) : std::string();
        };
        ProgrammeJob j;
        j.id = sqlite3_column_int64(s, 0);
        j.source = sqlite3_column_int64(s, 1);
        j.channel = text(2);
        j.title = text(3);
        j.start = sqlite3_column_int64(s, 4);
        j.end = sqlite3_column_int64(s, 5);
        j.drive = text(6);
        j.recording = sqlite3_column_int(s, 7) != 0;
        j.state = sqlite3_column_int(s, 8);
        j.result = text(9);
        if (j.id > 0 && j.source > 0 && !j.channel.empty() && j.channel.size() <= 256 &&
            j.title.size() <= 4096 && j.drive.size() <= 4096 && j.start > 0 && j.end > j.start &&
            j.end - j.start <= 86400 && j.state >= 0 && j.state <= 4)
            rows.push_back(std::move(j));
    }
    sqlite3_finalize(s);
    return rows;
}
bool ProgrammeJobs::save(ProgrammeJob &j)
{
    if (!db_ || j.source <= 0 || j.channel.empty() || j.channel.size() > 256 ||
        j.title.size() > 4096 || j.drive.size() > 4096 || j.start <= 0 || j.end <= j.start ||
        j.end - j.start > 86400)
        return false;
    const auto rows = list();
    if (rows.size() >= 128)
        return false;
    for (const auto &row : rows)
        if (row.state < 2 && row.recording && j.recording && row.start < j.end && j.start < row.end)
            return false;
    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "INSERT INTO jobs(source,channel,title,start,end,drive,recording) "
                           "VALUES(?,?,?,?,?,?,?)",
                           -1, &s, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(s, 1, j.source);
    sqlite3_bind_text(s, 2, j.channel.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 3, j.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 4, j.start);
    sqlite3_bind_int64(s, 5, j.end);
    sqlite3_bind_text(s, 6, j.drive.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(s, 7, j.recording);
    const bool ok = sqlite3_step(s) == SQLITE_DONE;
    sqlite3_finalize(s);
    if (ok)
        j.id = sqlite3_last_insert_rowid(db_);
    return ok;
}
bool ProgrammeJobs::finish(std::int64_t id, int state, const std::string &result)
{
    if (state < 1 || state > 4 || result.size() > 4096)
        return false;
    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(db_, "UPDATE jobs SET state=?,result=? WHERE id=?", -1, &s, nullptr) !=
        SQLITE_OK)
        return false;
    sqlite3_bind_int(s, 1, state);
    sqlite3_bind_text(s, 2, result.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 3, id);
    const bool ok = sqlite3_step(s) == SQLITE_DONE && sqlite3_changes(db_) == 1;
    sqlite3_finalize(s);
    return ok;
}
bool ProgrammeJobs::remove(std::int64_t id)
{
    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM jobs WHERE id=?", -1, &s, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(s, 1, id);
    const bool ok = sqlite3_step(s) == SQLITE_DONE;
    sqlite3_finalize(s);
    return ok;
}
Recorder::~Recorder()
{
    cancel();
    join();
}
void Recorder::cancel()
{
    stop_.store(true);
}
void Recorder::join()
{
    if (thread_)
    {
        platform::thread_join(thread_);
        thread_ = nullptr;
    }
}
bool Recorder::start(RecordingInput input, const std::string &drive, std::int64_t end,
                     std::uint64_t now, std::int64_t job)
{
    if (active() || end <= static_cast<std::int64_t>(now) ||
        end - static_cast<std::int64_t>(now) > 86400)
        return false;
    struct stat info
    {
    };
    if (lstat(drive.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
        return false;
    input_ = std::move(input);
    drive_ = drive;
    end_ = end;
    result_.clear();
    success_ = false;
    filename_ = drive + "/ProsperoTV-" + std::to_string(now) + "-" + std::to_string(job) + ".ts";
    stop_.store(false);
    done_.store(false);
    bytes_.store(0);
    thread_ = platform::thread_start(run, this, 2u * 1024u * 1024u, "iptv-record");
    return thread_ != nullptr;
}
void *Recorder::run(void *context)
{
    auto &r = *static_cast<Recorder *>(context);
    const std::string partial = r.filename_ + ".partial";
    struct stat mount
    {
    };
    const bool mounted = stat(r.drive_.c_str(), &mount) == 0;
    const int fd = mounted ? ::open(partial.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600) : -1;
    if (fd < 0)
    {
        r.result_ = "Cannot create recording on the USB drive";
        r.done_.store(true, std::memory_order_release);
        return nullptr;
    }
    struct Sink
    {
        Recorder *r;
        int fd;
        dev_t device;
        bool failed = false;
        std::uint64_t synced = 0;
    } sink{&r, fd, mount.st_dev};
    const iptv::http::RequestControl control{
        [](void *p)
        {
            auto &r = *static_cast<Recorder *>(p);
            return r.stop_.load() || platform::unix_time() >= static_cast<std::uint64_t>(r.end_);
        },
        &r};
    const iptv::http::ListSink output{
        [](void *p, const char *data, std::size_t size)
        {
            auto &s = *static_cast<Sink *>(p);
            struct stat current
            {
            };
            if (stat(s.r->drive_.c_str(), &current) != 0 || current.st_dev != s.device)
            {
                s.failed = true;
                return false;
            }
            for (std::size_t offset = 0; offset < size;)
            {
                const auto count = write(s.fd, data + offset, size - offset);
                if (count < 0 && errno == EINTR)
                    continue;
                if (count <= 0)
                {
                    s.failed = true;
                    return false;
                }
                offset += static_cast<std::size_t>(count);
                s.r->bytes_.fetch_add(static_cast<std::uint64_t>(count));
            }
            if (s.r->bytes_.load() - s.synced >= 4u * 1024u * 1024u)
            {
                if (fsync(s.fd) != 0)
                {
                    s.failed = true;
                    return false;
                }
                s.synced = s.r->bytes_.load();
            }
            return true;
        },
        &sink};
    int status = -1;
    bool ready = true;
    if (!r.input_.portal_command.empty())
    {
        ready = platform::network_init() == iptv::http::Status::ok;
        if (ready)
        {
            PortalClient portal({r.input_.portal_url, r.input_.portal_mac, r.input_.user_agent,
                                 r.input_.referrer, r.input_.extra_headers},
                                &control);
            ready = portal.sign_in() && portal.resolve(r.input_.portal_command, &r.input_.url);
            platform::network_shutdown();
        }
        if (!ready)
            r.result_ = "The portal recording address could not be resolved";
    }
    if (ready)
        status = platform::record_stream(r.input_, output, control, r.result_);
    const bool flushed = fsync(fd) == 0;
    const bool closed = ::close(fd) == 0;
    r.success_ = status >= 0 && !sink.failed && flushed && closed && r.bytes_.load() > 0;
    struct stat existing
    {
    };
    if (r.success_ && lstat(r.filename_.c_str(), &existing) != 0 && errno == ENOENT &&
        std::rename(partial.c_str(), r.filename_.c_str()) == 0)
        r.result_ = r.filename_;
    else
    {
        r.success_ = false;
        if (sink.failed || !flushed || !closed)
            r.result_ =
                "USB recording stopped: drive disconnected, full, or file size limit reached";
        if (r.result_.empty())
            r.result_ = "Recording failed; a partial file was retained";
    }
    r.done_.store(true, std::memory_order_release);
    return nullptr;
}
} // namespace ptv
