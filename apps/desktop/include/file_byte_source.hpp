#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

struct FileDocumentHandle;

class FileByteSource final {
public:
    FileByteSource() = default;
    ~FileByteSource();

    FileByteSource(const FileByteSource&) = delete;
    FileByteSource& operator=(const FileByteSource&) = delete;

    bool open(const QString& path);
    bool is_open() const;
    void close();

    QString file_path() const;
    QString display_name() const;
    qint64 size() const;
    QByteArray read_range(qint64 offset, qint64 length) const;
    bool read_byte(qint64 offset, quint8& value) const;
    bool overwrite_byte(qint64 offset, quint8 value);
    bool overwrite_range(qint64 offset, const QByteArray& bytes);
    bool insert_range(qint64 offset, const QByteArray& bytes);
    bool delete_range(qint64 offset, qint64 length);
    bool is_read_only() const;
    bool is_dirty() const;
    bool can_save_in_place() const;
    bool save();
    bool save_as(const QString& path);
    bool save_with_progress(const std::function<bool(qint64, qint64)>& progress_callback);
    bool save_as_with_progress(const QString& path, const std::function<bool(qint64, qint64)>& progress_callback);
    bool save_in_place_with_progress(const std::function<bool(qint64, qint64)>& progress_callback);
    bool undo();
    bool redo();

private:
    void clear_cache();

    QString path_;
    FileDocumentHandle* handle_ = nullptr;
    mutable qint64 cached_offset_ = -1;
    mutable QByteArray cached_bytes_;
};
