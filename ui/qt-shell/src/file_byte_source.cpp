#include "file_byte_source.hpp"

#include "hex_bridge.h"

#include <QFileInfo>

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

    const qint64 cache_window = qMax<qint64>(bounded_length, 256 * 1024);
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

    for (int index = 0; index < bytes.size(); ++index) {
        if (!overwrite_byte(offset + index, static_cast<quint8>(bytes.at(index)))) {
            return false;
        }
    }

    return true;
}

bool FileByteSource::is_dirty() const {
    return handle_ != nullptr && hm_file_document_is_dirty(handle_);
}

bool FileByteSource::save() {
    return save_as(path_);
}

bool FileByteSource::save_as(const QString& path) {
    if (handle_ == nullptr || path.isEmpty()) {
        return false;
    }

    const QByteArray utf8_path = path.toUtf8();
    const bool saved = hm_file_document_save(handle_, utf8_path.constData());
    if (!saved) {
        return false;
    }

    path_ = path;
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
