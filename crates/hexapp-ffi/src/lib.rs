use std::collections::BTreeMap;
use std::ffi::{c_char, c_void, CStr};
use std::fs;
use std::fs::{File, OpenOptions};
use std::io::{Seek, SeekFrom, Write};
use std::path::PathBuf;
use std::ptr;

use hexapp_core::{Document, DocumentSummary};
use hexapp_io::{ByteSource, FileByteSource};
use hexapp_session::{RecentFile, WindowSettings};

#[derive(Clone)]
struct EditRecord {
    offset: u64,
    before: Vec<u8>,
    after: Vec<u8>,
}

#[derive(Debug, Default)]
pub struct AppState {
    documents: Vec<Document>,
    recent_files: Vec<RecentFile>,
    settings: WindowSettings,
}

impl AppState {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn documents(&self) -> &[Document] {
        &self.documents
    }

    pub fn recent_files(&self) -> &[RecentFile] {
        &self.recent_files
    }

    pub fn settings(&self) -> &WindowSettings {
        &self.settings
    }

    pub fn create_scratch_document(&mut self, title: impl Into<String>) -> &DocumentSummary {
        let id = self.documents.len() as u64 + 1;
        self.documents.push(Document::scratch(id, title));
        self.documents
            .last()
            .expect("scratch document was just pushed")
            .summary()
    }

    pub fn open_file_stub(
        &mut self,
        path: impl AsRef<std::path::Path>,
        len: u64,
        read_only: bool,
    ) -> &DocumentSummary {
        let id = self.documents.len() as u64 + 1;
        self.documents
            .push(Document::file_backed(id, path, len, read_only));
        self.documents
            .last()
            .expect("file-backed document was just pushed")
            .summary()
    }
}

pub struct FileDocumentHandle {
    #[allow(dead_code)]
    path: PathBuf,
    source: FileByteSource,
    materialized: Option<Vec<u8>>,
    overwrites: BTreeMap<u64, u8>,
    undo_stack: Vec<EditRecord>,
    redo_stack: Vec<EditRecord>,
}

type SaveProgressCallback = extern "C" fn(completed: u64, total: u64, user_data: *mut c_void) -> bool;

fn path_from_c(path: *const c_char) -> Option<PathBuf> {
    if path.is_null() {
        return None;
    }

    let c_str = unsafe { CStr::from_ptr(path) };
    let utf8 = c_str.to_str().ok()?;
    Some(PathBuf::from(utf8))
}

fn original_byte(document: &FileDocumentHandle, offset: u64) -> Option<u8> {
    if let Some(materialized) = &document.materialized {
        return materialized.get(offset as usize).copied();
    }

    let bytes = document
        .source
        .read_range(hexapp_core::ByteRange {
            start: offset,
            len: 1,
        })
        .ok()?;
    bytes.first().copied()
}

fn current_byte(document: &FileDocumentHandle, offset: u64) -> Option<u8> {
    if let Some(materialized) = &document.materialized {
        return materialized.get(offset as usize).copied();
    }

    if let Some(overwritten) = document.overwrites.get(&offset) {
        return Some(*overwritten);
    }

    original_byte(document, offset)
}

fn current_len(document: &FileDocumentHandle) -> u64 {
    document
        .materialized
        .as_ref()
        .map(|bytes| bytes.len() as u64)
        .unwrap_or_else(|| document.source.len())
}

fn current_bytes(document: &FileDocumentHandle, offset: u64, len: u64) -> Option<Vec<u8>> {
    if let Some(materialized) = &document.materialized {
        let start = offset as usize;
        let end = start.checked_add(len as usize)?;
        return materialized.get(start..end).map(|slice| slice.to_vec());
    }

    let mut bytes = document
        .source
        .read_range(hexapp_core::ByteRange { start: offset, len })
        .ok()?;

    for (index, byte) in bytes.iter_mut().enumerate() {
        if let Some(overwritten) = document.overwrites.get(&(offset + index as u64)) {
            *byte = *overwritten;
        }
    }

    Some(bytes)
}

fn materialize_document(document: &mut FileDocumentHandle) -> bool {
    if document.materialized.is_some() {
        return true;
    }

    let Some(bytes) = current_bytes(document, 0, document.source.len()) else {
        return false;
    };

    document.materialized = Some(bytes);
    document.overwrites.clear();
    true
}

fn apply_byte(document: &mut FileDocumentHandle, offset: u64, value: u8) -> bool {
    let Some(original) = original_byte(document, offset) else {
        return false;
    };

    if value == original {
        document.overwrites.remove(&offset);
    } else {
        document.overwrites.insert(offset, value);
    }

    true
}

fn apply_bytes(document: &mut FileDocumentHandle, offset: u64, bytes: &[u8]) -> bool {
    if let Some(materialized) = &mut document.materialized {
        let start = offset as usize;
        let end = match start.checked_add(bytes.len()) {
            Some(value) => value,
            None => return false,
        };
        let Some(target) = materialized.get_mut(start..end) else {
            return false;
        };
        target.copy_from_slice(bytes);
        return true;
    }

    let Some(original_bytes) = document
        .source
        .read_range(hexapp_core::ByteRange {
            start: offset,
            len: bytes.len() as u64,
        })
        .ok()
    else {
        return false;
    };

    for (index, value) in bytes.iter().copied().enumerate() {
        let original = original_bytes[index];
        let absolute_offset = offset + index as u64;
        if value == original {
            document.overwrites.remove(&absolute_offset);
        } else {
            document.overwrites.insert(absolute_offset, value);
        }
    }

    true
}

fn replace_range(document: &mut FileDocumentHandle, offset: u64, remove_len: u64, replacement: &[u8]) -> bool {
    if document.materialized.is_none() && remove_len != replacement.len() as u64 && !materialize_document(document) {
        return false;
    }

    if let Some(materialized) = &mut document.materialized {
        let start = offset as usize;
        let end = match start.checked_add(remove_len as usize) {
            Some(value) => value,
            None => return false,
        };
        if end > materialized.len() {
            return false;
        }

        materialized.splice(start..end, replacement.iter().copied());
        return true;
    }

    if remove_len != replacement.len() as u64 {
        return false;
    }

    apply_bytes(document, offset, replacement)
}

fn report_save_progress(
    callback: Option<SaveProgressCallback>,
    user_data: *mut c_void,
    completed: u64,
    total: u64,
) -> bool {
    match callback {
        Some(callback) => callback(completed, total, user_data),
        None => true,
    }
}

fn save_document(
    document: &mut FileDocumentHandle,
    target_path: &PathBuf,
    progress_callback: Option<SaveProgressCallback>,
    user_data: *mut c_void,
) -> bool {
    let mut output = match File::create(target_path) {
        Ok(file) => file,
        Err(_) => return false,
    };

    let total_len = current_len(document);
    if !report_save_progress(progress_callback, user_data, 0, total_len) {
        return false;
    }

    if let Some(materialized) = &document.materialized {
        const CHUNK_SIZE: usize = 8 * 1024 * 1024;
        let mut written = 0_u64;
        for chunk in materialized.chunks(CHUNK_SIZE) {
            if output.write_all(chunk).is_err() {
                return false;
            }
            written += chunk.len() as u64;
            if !report_save_progress(progress_callback, user_data, written, total_len) {
                return false;
            }
        }
        if output.flush().is_err() {
            return false;
        }

        let Ok(source) = FileByteSource::open(target_path) else {
            return false;
        };

        document.path = target_path.clone();
        document.source = source;
        document.materialized = None;
        document.overwrites.clear();
        document.undo_stack.clear();
        document.redo_stack.clear();
        return true;
    }

    let mut offset = 0_u64;
    const CHUNK_SIZE: u64 = 8 * 1024 * 1024;

    while offset < document.source.len() {
        let len = (document.source.len() - offset).min(CHUNK_SIZE);
        let Ok(mut bytes) = document.source.read_range(hexapp_core::ByteRange { start: offset, len }) else {
            return false;
        };

        for (index, byte) in bytes.iter_mut().enumerate() {
            if let Some(overwritten) = document.overwrites.get(&(offset + index as u64)) {
                *byte = *overwritten;
            }
        }

        if output.write_all(&bytes).is_err() {
            return false;
        }

        offset += bytes.len() as u64;
        if !report_save_progress(progress_callback, user_data, offset, total_len) {
            return false;
        }
    }

    if output.flush().is_err() {
        return false;
    }

    let Ok(source) = FileByteSource::open(target_path) else {
        return false;
    };

    document.path = target_path.clone();
    document.source = source;
    document.materialized = None;
    document.overwrites.clear();
    document.undo_stack.clear();
    document.redo_stack.clear();
    true
}

fn save_document_in_place(
    document: &mut FileDocumentHandle,
    progress_callback: Option<SaveProgressCallback>,
    user_data: *mut c_void,
) -> bool {
    if document.materialized.is_some() {
        return false;
    }

    let total_dirty = document.overwrites.len() as u64;
    if !report_save_progress(progress_callback, user_data, 0, total_dirty) {
        return false;
    }
    if total_dirty == 0 {
        document.undo_stack.clear();
        document.redo_stack.clear();
        return true;
    }

    let mut output = match OpenOptions::new().write(true).open(&document.path) {
        Ok(file) => file,
        Err(_) => return false,
    };

    let mut written = 0_u64;
    let mut current_start = 0_u64;
    let mut current_bytes: Vec<u8> = Vec::new();
    let mut previous_offset = None::<u64>;

    let flush_run = |output: &mut File, start: u64, bytes: &[u8]| -> bool {
        if bytes.is_empty() {
            return true;
        }
        if output.seek(SeekFrom::Start(start)).is_err() {
            return false;
        }
        output.write_all(bytes).is_ok()
    };

    for (&offset, &value) in document.overwrites.iter() {
        match previous_offset {
            Some(prev) if offset == prev + 1 => {
                current_bytes.push(value);
            }
            Some(_) => {
                if !flush_run(&mut output, current_start, &current_bytes) {
                    return false;
                }
                written += current_bytes.len() as u64;
                if !report_save_progress(progress_callback, user_data, written, total_dirty) {
                    return false;
                }
                current_start = offset;
                current_bytes.clear();
                current_bytes.push(value);
            }
            None => {
                current_start = offset;
                current_bytes.push(value);
            }
        }
        previous_offset = Some(offset);
    }

    if !flush_run(&mut output, current_start, &current_bytes) {
        return false;
    }
    written += current_bytes.len() as u64;
    if !report_save_progress(progress_callback, user_data, written, total_dirty) {
        return false;
    }

    if output.flush().is_err() {
        return false;
    }

    let Ok(source) = FileByteSource::open(&document.path) else {
        return false;
    };
    document.source = source;
    document.overwrites.clear();
    document.undo_stack.clear();
    document.redo_stack.clear();
    true
}

fn temp_save_path_for(target_path: &PathBuf) -> PathBuf {
    let mut temp_path = target_path.clone();
    let temp_extension = match target_path.extension().and_then(|ext| ext.to_str()) {
        Some(ext) if !ext.is_empty() => format!("{ext}.hexmaster.tmp"),
        _ => String::from("hexmaster.tmp"),
    };
    temp_path.set_extension(temp_extension);
    temp_path
}

fn backup_save_path_for(target_path: &PathBuf) -> PathBuf {
    let mut backup_path = target_path.clone();
    let backup_extension = match target_path.extension().and_then(|ext| ext.to_str()) {
        Some(ext) if !ext.is_empty() => format!("{ext}.hexmaster.bak"),
        _ => String::from("hexmaster.bak"),
    };
    backup_path.set_extension(backup_extension);
    backup_path
}

fn replace_file_with_backup(temp_path: &PathBuf, target_path: &PathBuf) -> bool {
    let backup_path = backup_save_path_for(target_path);
    let _ = fs::remove_file(&backup_path);

    if target_path.exists() {
        if fs::rename(target_path, &backup_path).is_err() {
            let _ = fs::remove_file(temp_path);
            return false;
        }
    }

    if fs::rename(temp_path, target_path).is_ok() {
        let _ = fs::remove_file(&backup_path);
        return true;
    }

    let _ = fs::rename(&backup_path, target_path);
    let _ = fs::remove_file(temp_path);
    false
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_open(path: *const c_char) -> *mut FileDocumentHandle {
    let Some(path) = path_from_c(path) else {
        return ptr::null_mut();
    };

    let Ok(source) = FileByteSource::open(&path) else {
        return ptr::null_mut();
    };

    Box::into_raw(Box::new(FileDocumentHandle {
        path,
        source,
        materialized: None,
        overwrites: BTreeMap::new(),
        undo_stack: Vec::new(),
        redo_stack: Vec::new(),
    }))
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_close(handle: *mut FileDocumentHandle) {
    if handle.is_null() {
        return;
    }

    unsafe {
        drop(Box::from_raw(handle));
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_size(handle: *const FileDocumentHandle) -> u64 {
    if handle.is_null() {
        return 0;
    }

    unsafe { current_len(&*handle) }
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_read(
    handle: *const FileDocumentHandle,
    offset: u64,
    length: usize,
    out_buffer: *mut u8,
) -> usize {
    if handle.is_null() || out_buffer.is_null() || length == 0 {
        return 0;
    }

    let document = unsafe { &*handle };
    let Some(bytes) = current_bytes(document, offset, length as u64) else {
        return 0;
    };

    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), out_buffer, bytes.len());
    }

    bytes.len()
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_overwrite_byte(
    handle: *mut FileDocumentHandle,
    offset: u64,
    value: u8,
) -> bool {
    if handle.is_null() {
        return false;
    }

    let document = unsafe { &mut *handle };
    if offset >= current_len(document) {
        return false;
    }

    let Some(before) = current_byte(document, offset) else {
        return false;
    };

    if before == value {
        return true;
    }

    if !apply_byte(document, offset, value) {
        return false;
    }

    document.undo_stack.push(EditRecord {
        offset,
        before: vec![before],
        after: vec![value],
    });
    document.redo_stack.clear();
    true
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_overwrite_range(
    handle: *mut FileDocumentHandle,
    offset: u64,
    bytes: *const u8,
    length: usize,
) -> bool {
    if handle.is_null() || bytes.is_null() || length == 0 {
        return false;
    }

    let document = unsafe { &mut *handle };
    let end = offset.saturating_add(length as u64);
    if end > current_len(document) {
        return false;
    }

    let replacement = unsafe { std::slice::from_raw_parts(bytes, length) };
    let Some(before_bytes) = current_bytes(document, offset, length as u64) else {
        return false;
    };

    if before_bytes == replacement {
        return true;
    }

    if !apply_bytes(document, offset, replacement) {
        return false;
    }

    document.undo_stack.push(EditRecord {
        offset,
        before: before_bytes,
        after: replacement.to_vec(),
    });
    document.redo_stack.clear();
    true
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_insert_range(
    handle: *mut FileDocumentHandle,
    offset: u64,
    bytes: *const u8,
    length: usize,
) -> bool {
    if handle.is_null() || bytes.is_null() || length == 0 {
        return false;
    }

    let document = unsafe { &mut *handle };
    if offset > current_len(document) || !materialize_document(document) {
        return false;
    }

    let replacement = unsafe { std::slice::from_raw_parts(bytes, length) };
    if !replace_range(document, offset, 0, replacement) {
        return false;
    }

    document.undo_stack.push(EditRecord {
        offset,
        before: Vec::new(),
        after: replacement.to_vec(),
    });
    document.redo_stack.clear();
    true
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_delete_range(
    handle: *mut FileDocumentHandle,
    offset: u64,
    length: usize,
) -> bool {
    if handle.is_null() || length == 0 {
        return false;
    }

    let document = unsafe { &mut *handle };
    let end = offset.saturating_add(length as u64);
    if end > current_len(document) {
        return false;
    }

    let Some(before) = current_bytes(document, offset, length as u64) else {
        return false;
    };
    if !materialize_document(document) || !replace_range(document, offset, length as u64, &[]) {
        return false;
    }

    document.undo_stack.push(EditRecord {
        offset,
        before,
        after: Vec::new(),
    });
    document.redo_stack.clear();
    true
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_is_dirty(handle: *const FileDocumentHandle) -> bool {
    if handle.is_null() {
        return false;
    }

    unsafe {
        (*handle).materialized.is_some() || !(*handle).overwrites.is_empty()
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_is_read_only(handle: *const FileDocumentHandle) -> bool {
    if handle.is_null() {
        return false;
    }

    unsafe { (*handle).source.metadata().read_only }
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_can_save_in_place(handle: *const FileDocumentHandle) -> bool {
    if handle.is_null() {
        return false;
    }

    let document = unsafe { &*handle };
    !document.source.metadata().read_only && document.materialized.is_none() && !document.overwrites.is_empty()
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_undo(handle: *mut FileDocumentHandle) -> bool {
    if handle.is_null() {
        return false;
    }

    let document = unsafe { &mut *handle };
    let Some(edit) = document.undo_stack.pop() else {
        return false;
    };

    if !replace_range(document, edit.offset, edit.after.len() as u64, &edit.before) {
        return false;
    }

    document.redo_stack.push(edit);
    true
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_redo(handle: *mut FileDocumentHandle) -> bool {
    if handle.is_null() {
        return false;
    }

    let document = unsafe { &mut *handle };
    let Some(edit) = document.redo_stack.pop() else {
        return false;
    };

    if !replace_range(document, edit.offset, edit.before.len() as u64, &edit.after) {
        return false;
    }

    document.undo_stack.push(edit);
    true
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_save(handle: *mut FileDocumentHandle, path: *const c_char) -> bool {
    hm_file_document_save_with_progress(handle, path, None, ptr::null_mut())
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_save_with_progress(
    handle: *mut FileDocumentHandle,
    path: *const c_char,
    progress_callback: Option<SaveProgressCallback>,
    user_data: *mut c_void,
) -> bool {
    if handle.is_null() {
        return false;
    }

    let Some(target_path) = path_from_c(path) else {
        return false;
    };

    let document = unsafe { &mut *handle };

    if document.path == target_path {
        let temp_path = temp_save_path_for(&target_path);
        if !save_document(document, &temp_path, progress_callback, user_data) {
            let _ = fs::remove_file(&temp_path);
            return false;
        }

        if !replace_file_with_backup(&temp_path, &target_path) {
            return false;
        }

        let Ok(source) = FileByteSource::open(&target_path) else {
            return false;
        };
        document.path = target_path;
        document.source = source;
        document.overwrites.clear();
        document.undo_stack.clear();
        document.redo_stack.clear();
        return true;
    }

    save_document(document, &target_path, progress_callback, user_data)
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_save_in_place_with_progress(
    handle: *mut FileDocumentHandle,
    progress_callback: Option<SaveProgressCallback>,
    user_data: *mut c_void,
) -> bool {
    if handle.is_null() {
        return false;
    }

    let document = unsafe { &mut *handle };
    if document.source.metadata().read_only {
        return false;
    }

    save_document_in_place(document, progress_callback, user_data)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;
    use std::fs;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn unique_temp_path(name: &str) -> PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system time before epoch")
            .as_nanos();
        std::env::temp_dir().join(format!("hex_master_ffi_{name}_{nonce}.bin"))
    }

    fn create_temp_file(name: &str, bytes: &[u8]) -> PathBuf {
        let path = unique_temp_path(name);
        fs::write(&path, bytes).expect("write temp file");
        path
    }

    fn open_handle(path: &PathBuf) -> *mut FileDocumentHandle {
        let path_c = CString::new(path.to_string_lossy().as_bytes()).expect("cstring path");
        let handle = hm_file_document_open(path_c.as_ptr());
        assert!(!handle.is_null(), "open handle");
        handle
    }

    fn read_all(handle: *const FileDocumentHandle) -> Vec<u8> {
        let len = hm_file_document_size(handle) as usize;
        let mut bytes = vec![0_u8; len];
        let read = hm_file_document_read(handle, 0, len, bytes.as_mut_ptr());
        assert_eq!(read, len);
        bytes
    }

    #[test]
    fn overwrite_range_updates_visible_bytes_and_undo_redo() {
        let path = create_temp_file("overwrite_range", &[0x10, 0x20, 0x30, 0x40, 0x50]);
        let handle = open_handle(&path);

        let replacement = [0xAA, 0xBB, 0xCC];
        assert!(hm_file_document_overwrite_range(
            handle,
            1,
            replacement.as_ptr(),
            replacement.len()
        ));
        assert_eq!(read_all(handle), vec![0x10, 0xAA, 0xBB, 0xCC, 0x50]);
        assert!(hm_file_document_is_dirty(handle));

        assert!(hm_file_document_undo(handle));
        assert_eq!(read_all(handle), vec![0x10, 0x20, 0x30, 0x40, 0x50]);
        assert!(!hm_file_document_undo(handle));

        assert!(hm_file_document_redo(handle));
        assert_eq!(read_all(handle), vec![0x10, 0xAA, 0xBB, 0xCC, 0x50]);
        assert!(!hm_file_document_redo(handle));

        hm_file_document_close(handle);
        fs::remove_file(path).expect("cleanup temp file");
    }

    #[test]
    fn save_persists_range_overwrite_to_same_path() {
        let path = create_temp_file("save_same_path", &[1, 2, 3, 4, 5, 6]);
        let handle = open_handle(&path);

        let replacement = [9, 8, 7];
        assert!(hm_file_document_overwrite_range(
            handle,
            2,
            replacement.as_ptr(),
            replacement.len()
        ));

        let path_c = CString::new(path.to_string_lossy().as_bytes()).expect("cstring path");
        assert!(hm_file_document_save(handle, path_c.as_ptr()));
        assert!(!hm_file_document_is_dirty(handle));
        assert_eq!(fs::read(&path).expect("read saved file"), vec![1, 2, 9, 8, 7, 6]);

        hm_file_document_close(handle);
        fs::remove_file(path).expect("cleanup temp file");
    }

    #[test]
    fn save_as_writes_modified_contents_to_new_path() {
        let source_path = create_temp_file("save_as_source", &[0xDE, 0xAD, 0xBE, 0xEF]);
        let target_path = unique_temp_path("save_as_target");
        let handle = open_handle(&source_path);

        let replacement = [0x11, 0x22];
        assert!(hm_file_document_overwrite_range(
            handle,
            1,
            replacement.as_ptr(),
            replacement.len()
        ));

        let target_c = CString::new(target_path.to_string_lossy().as_bytes()).expect("cstring target");
        assert!(hm_file_document_save(handle, target_c.as_ptr()));
        assert_eq!(fs::read(&target_path).expect("read save-as file"), vec![0xDE, 0x11, 0x22, 0xEF]);
        assert!(!hm_file_document_is_dirty(handle));
        assert_eq!(read_all(handle), vec![0xDE, 0x11, 0x22, 0xEF]);

        hm_file_document_close(handle);
        fs::remove_file(source_path).expect("cleanup source");
        fs::remove_file(target_path).expect("cleanup target");
    }

    #[test]
    fn overwrite_only_documents_can_save_in_place() {
        let path = create_temp_file("save_in_place_fast", &[0x10, 0x20, 0x30, 0x40]);
        let handle = open_handle(&path);

        let replacement = [0xAA, 0xBB];
        assert!(hm_file_document_overwrite_range(
            handle,
            1,
            replacement.as_ptr(),
            replacement.len()
        ));
        assert!(hm_file_document_can_save_in_place(handle));
        assert!(hm_file_document_save_in_place_with_progress(handle, None, ptr::null_mut()));
        assert_eq!(fs::read(&path).expect("read saved file"), vec![0x10, 0xAA, 0xBB, 0x40]);
        assert!(!hm_file_document_is_dirty(handle));

        hm_file_document_close(handle);
        fs::remove_file(path).expect("cleanup temp file");
    }

    #[test]
    fn structural_edits_do_not_use_in_place_save() {
        let path = create_temp_file("save_in_place_structural", &[1, 2, 3, 4]);
        let handle = open_handle(&path);

        let inserted = [9, 9];
        assert!(hm_file_document_insert_range(
            handle,
            2,
            inserted.as_ptr(),
            inserted.len()
        ));
        assert!(hm_file_document_is_dirty(handle));
        assert!(!hm_file_document_can_save_in_place(handle));
        assert!(!hm_file_document_save_in_place_with_progress(handle, None, ptr::null_mut()));

        hm_file_document_close(handle);
        fs::remove_file(path).expect("cleanup temp file");
    }

    #[test]
    fn insert_and_delete_range_update_document_length() {
        let path = create_temp_file("insert_delete", &[0x41, 0x42, 0x43, 0x44]);
        let handle = open_handle(&path);

        let inserted = [0x99, 0x98];
        assert!(hm_file_document_insert_range(
            handle,
            2,
            inserted.as_ptr(),
            inserted.len()
        ));
        assert_eq!(hm_file_document_size(handle), 6);
        assert_eq!(read_all(handle), vec![0x41, 0x42, 0x99, 0x98, 0x43, 0x44]);

        assert!(hm_file_document_delete_range(handle, 1, 3));
        assert_eq!(hm_file_document_size(handle), 3);
        assert_eq!(read_all(handle), vec![0x41, 0x43, 0x44]);

        assert!(hm_file_document_undo(handle));
        assert_eq!(read_all(handle), vec![0x41, 0x42, 0x99, 0x98, 0x43, 0x44]);
        assert!(hm_file_document_undo(handle));
        assert_eq!(read_all(handle), vec![0x41, 0x42, 0x43, 0x44]);

        hm_file_document_close(handle);
        fs::remove_file(path).expect("cleanup temp file");
    }
}
