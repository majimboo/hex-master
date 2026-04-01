#include "file_byte_source.hpp"

#include "hex_bridge.h"

#include <QFileInfo>

namespace {
constexpr qint64 kReadCacheWindow = 256 * 1024;

struct SaveProgressContext {
    const std::function<bool(qint64, qint64)>* callback = nullptr;
};

bool save_progress_thunk(std::uint64_t completed, std::uint64_t total, void* user_data) {
    auto* context = static_cast<SaveProgressContext*>(user_data);
    if (context == nullptr || context->callback == nullptr) {
        return true;
    }

    return (*context->callback)(static_cast<qint64>(completed), static_cast<qint64>(total));
}
}

FileByteSource::~FileByteSource() {
    close();
}

bool FileByteSource::open(const QString& path) {
    close();

    const QByteArray utf8_path = path.toUtf8();
    handle_ = hm_file_document_open(utf8_path.constData());
    if (handle_ == nullptr) {
        return false;
    }

    path_ = path;
    clear_cache();
    return true;
}

bool FileByteSource::is_open() const {
    return handle_ != nullptr;
}

void FileByteSource::close() {
    if (handle_ != nullptr) {
        hm_file_document_close(handle_);
        handle_ = nullptr;
    }

    path_.clear();
    clear_cache();
}

QString FileByteSource::file_path() const {
    return path_;
}

QString FileByteSource::display_name() const {
    const QFileInfo info(path_);
    return info.fileName().isEmpty() ? QStringLiteral("Untitled") : info.fileName();
}

qint64 FileByteSource::size() const {
    return handle_ == nullptr ? 0 : static_cast<qint64>(hm_file_document_size(handle_));
}

QByteArray FileByteSource::read_range(qint64 offset, qint64 length) const {
    if (handle_ == nullptr || offset < 0 || length <= 0) {
        return {};
    }

    const qint64 file_size = size();
    if (offset >= file_size) {
        return {};
    }

    const qint64 bounded_length = qMin(length, file_size - offset);
    if (cached_offset_ >= 0 && offset >= cached_offset_ &&
        offset + bounded_length <= cached_offset_ + cached_bytes_.size()) {
        const qint64 start = offset - cached_offset_;
        return cached_bytes_.mid(start, bounded_length);
    }

    // Fill a larger local cache for sequential reads, but never ask the backend
    // for bytes beyond the actual end of the document.
    const qint64 remaining = file_size - offset;
    const qint64 cache_window = qMin<qint64>(remaining, qMax<qint64>(bounded_length, kReadCacheWindow));
    cached_bytes_.resize(static_cast<int>(cache_window));
    const std::size_t bytes_read = hm_file_document_read(
        handle_,
        static_cast<std::uint64_t>(offset),
        static_cast<std::size_t>(cache_window),
        reinterpret_cast<std::uint8_t*>(cached_bytes_.data()));

    cached_offset_ = offset;
    cached_bytes_.resize(static_cast<int>(bytes_read));
    return cached_bytes_.left(static_cast<int>(bounded_length));
}

bool FileByteSource::read_byte(qint64 offset, quint8& value) const {
    const QByteArray bytes = read_range(offset, 1);
    if (bytes.size() != 1) {
        return false;
    }

    value = static_cast<quint8>(bytes.at(0));
    return true;
}

bool FileByteSource::overwrite_byte(qint64 offset, quint8 value) {
    if (handle_ == nullptr || offset < 0) {
        return false;
    }

    const bool written = hm_file_document_overwrite_byte(
        handle_,
        static_cast<std::uint64_t>(offset),
        static_cast<std::uint8_t>(value));
    if (!written) {
        return false;
    }

    if (cached_offset_ >= 0 && offset >= cached_offset_ && offset < cached_offset_ + cached_bytes_.size()) {
        cached_bytes_[static_cast<int>(offset - cached_offset_)] = static_cast<char>(value);
    }

    return true;
}

bool FileByteSource::overwrite_range(qint64 offset, const QByteArray& bytes) {
    if (handle_ == nullptr || offset < 0 || bytes.isEmpty()) {
        return false;
    }

    const bool written = hm_file_document_overwrite_range(
        handle_,
        static_cast<std::uint64_t>(offset),
        reinterpret_cast<const std::uint8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size()));
    if (!written) {
        return false;
    }

    if (cached_offset_ >= 0) {
        const qint64 cache_end = cached_offset_ + cached_bytes_.size();
        const qint64 write_end = offset + bytes.size();
        if (offset < cache_end && write_end > cached_offset_) {
            const qint64 overlap_start = qMax(offset, cached_offset_);
            const qint64 overlap_end = qMin(write_end, cache_end);
            for (qint64 current = overlap_start; current < overlap_end; ++current) {
                cached_bytes_[static_cast<int>(current - cached_offset_)] =
                    bytes.at(static_cast<int>(current - offset));
            }
        }
    }

    return true;
}

bool FileByteSource::insert_range(qint64 offset, const QByteArray& bytes) {
    if (handle_ == nullptr || offset < 0 || bytes.isEmpty()) {
        return false;
    }

    const bool inserted = hm_file_document_insert_range(
        handle_,
        static_cast<std::uint64_t>(offset),
        reinterpret_cast<const std::uint8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size()));
    if (inserted) {
        clear_cache();
    }
    return inserted;
}

bool FileByteSource::delete_range(qint64 offset, qint64 length) {
    if (handle_ == nullptr || offset < 0 || length <= 0) {
        return false;
    }

    const bool deleted = hm_file_document_delete_range(
        handle_,
        static_cast<std::uint64_t>(offset),
        static_cast<std::size_t>(length));
    if (deleted) {
        clear_cache();
    }
    return deleted;
}

bool FileByteSource::is_read_only() const {
    return handle_ != nullptr && hm_file_document_is_read_only(handle_);
}

bool FileByteSource::is_dirty() const {
    return handle_ != nullptr && hm_file_document_is_dirty(handle_);
}

bool FileByteSource::can_save_in_place() const {
    return handle_ != nullptr && hm_file_document_can_save_in_place(handle_);
}

bool FileByteSource::save() {
    return save_as_with_progress(path_, {});
}

bool FileByteSource::save_as(const QString& path) {
    return save_as_with_progress(path, {});
}

bool FileByteSource::save_with_progress(const std::function<bool(qint64, qint64)>& progress_callback) {
    return save_as_with_progress(path_, progress_callback);
}

bool FileByteSource::save_as_with_progress(const QString& path, const std::function<bool(qint64, qint64)>& progress_callback) {
    if (handle_ == nullptr || path.isEmpty()) {
        return false;
    }

    const QByteArray utf8_path = path.toUtf8();
    SaveProgressContext context{&progress_callback};
    const bool saved = progress_callback
        ? hm_file_document_save_with_progress(handle_, utf8_path.constData(), save_progress_thunk, &context)
        : hm_file_document_save(handle_, utf8_path.constData());
    if (!saved) {
        return false;
    }

    path_ = path;
    clear_cache();
    return true;
}

bool FileByteSource::save_in_place_with_progress(const std::function<bool(qint64, qint64)>& progress_callback) {
    if (handle_ == nullptr || path_.isEmpty()) {
        return false;
    }

    SaveProgressContext context{&progress_callback};
    const bool saved = progress_callback
        ? hm_file_document_save_in_place_with_progress(handle_, save_progress_thunk, &context)
        : hm_file_document_save_in_place_with_progress(handle_, nullptr, nullptr);
    if (!saved) {
        return false;
    }

    clear_cache();
    return true;
}

bool FileByteSource::undo() {
    if (handle_ == nullptr) {
        return false;
    }

    const bool undone = hm_file_document_undo(handle_);
    if (undone) {
        clear_cache();
    }
    return undone;
}

bool FileByteSource::redo() {
    if (handle_ == nullptr) {
        return false;
    }

    const bool redone = hm_file_document_redo(handle_);
    if (redone) {
        clear_cache();
    }
    return redone;
}

void FileByteSource::clear_cache() {
    cached_offset_ = -1;
    cached_bytes_.clear();
}
