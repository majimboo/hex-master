#pragma once

#include <cstddef>
#include <cstdint>

struct FileDocumentHandle;
using HmSaveProgressCallback = bool (*)(std::uint64_t completed, std::uint64_t total, void* user_data);

extern "C" {
FileDocumentHandle* hm_file_document_open(const char* path);
void hm_file_document_close(FileDocumentHandle* handle);
std::uint64_t hm_file_document_size(const FileDocumentHandle* handle);
std::size_t hm_file_document_read(
    const FileDocumentHandle* handle,
    std::uint64_t offset,
    std::size_t length,
    std::uint8_t* out_buffer);
bool hm_file_document_overwrite_byte(
    FileDocumentHandle* handle,
    std::uint64_t offset,
    std::uint8_t value);
bool hm_file_document_overwrite_range(
    FileDocumentHandle* handle,
    std::uint64_t offset,
    const std::uint8_t* bytes,
    std::size_t length);
bool hm_file_document_insert_range(
    FileDocumentHandle* handle,
    std::uint64_t offset,
    const std::uint8_t* bytes,
    std::size_t length);
bool hm_file_document_delete_range(
    FileDocumentHandle* handle,
    std::uint64_t offset,
    std::size_t length);
bool hm_file_document_is_read_only(const FileDocumentHandle* handle);
bool hm_file_document_is_dirty(const FileDocumentHandle* handle);
bool hm_file_document_save(FileDocumentHandle* handle, const char* path);
bool hm_file_document_save_with_progress(
    FileDocumentHandle* handle,
    const char* path,
    HmSaveProgressCallback progress_callback,
    void* user_data);
bool hm_file_document_undo(FileDocumentHandle* handle);
bool hm_file_document_redo(FileDocumentHandle* handle);
}
