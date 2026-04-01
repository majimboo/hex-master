#pragma once

#include <QByteArray>
#include <QString>

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
    bool is_dirty() const;
    bool save();
    bool save_as(const QString& path);
    bool undo();
    bool redo();

private:
    void clear_cache();

    QString path_;
    FileDocumentHandle* handle_ = nullptr;
    mutable qint64 cached_offset_ = -1;
    mutable QByteArray cached_bytes_;
};
