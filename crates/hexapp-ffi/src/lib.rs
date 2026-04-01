use std::collections::BTreeMap;
use std::ffi::{c_char, CStr};
use std::fs;
use std::fs::File;
use std::io::Write;
use std::path::PathBuf;
use std::ptr;

use hexapp_core::{Document, DocumentSummary};
use hexapp_io::{ByteSource, FileByteSource};
use hexapp_session::{RecentFile, WindowSettings};

#[derive(Clone, Copy)]
struct OverwriteEdit {
    offset: u64,
    before: u8,
    after: u8,
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
    overwrites: BTreeMap<u64, u8>,
    undo_stack: Vec<OverwriteEdit>,
    redo_stack: Vec<OverwriteEdit>,
}

fn path_from_c(path: *const c_char) -> Option<PathBuf> {
    if path.is_null() {
        return None;
    }

    let c_str = unsafe { CStr::from_ptr(path) };
    let utf8 = c_str.to_str().ok()?;
    Some(PathBuf::from(utf8))
}

fn original_byte(document: &FileDocumentHandle, offset: u64) -> Option<u8> {
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
    if let Some(overwritten) = document.overwrites.get(&offset) {
        return Some(*overwritten);
    }

    original_byte(document, offset)
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

fn save_document(document: &mut FileDocumentHandle, target_path: &PathBuf) -> bool {
    let mut output = match File::create(target_path) {
        Ok(file) => file,
        Err(_) => return false,
    };

    let mut offset = 0_u64;
    const CHUNK_SIZE: u64 = 1024 * 1024;

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
    }

    if output.flush().is_err() {
        return false;
    }

    let Ok(source) = FileByteSource::open(target_path) else {
        return false;
    };

    document.path = target_path.clone();
    document.source = source;
    document.overwrites.clear();
    document.undo_stack.clear();
    document.redo_stack.clear();
    true
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

    unsafe { (*handle).source.len() }
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

    let Ok(mut bytes) = (unsafe {
        (*handle).source.read_range(hexapp_core::ByteRange {
            start: offset,
            len: length as u64,
        })
    }) else {
        return 0;
    };

    let document = unsafe { &*handle };
    for (index, byte) in bytes.iter_mut().enumerate() {
        if let Some(overwritten) = document.overwrites.get(&(offset + index as u64)) {
            *byte = *overwritten;
        }
    }

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
    if offset >= document.source.len() {
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

    document.undo_stack.push(OverwriteEdit {
        offset,
        before,
        after: value,
    });
    document.redo_stack.clear();
    true
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_is_dirty(handle: *const FileDocumentHandle) -> bool {
    if handle.is_null() {
        return false;
    }

    unsafe { !(*handle).overwrites.is_empty() }
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

    if !apply_byte(document, edit.offset, edit.before) {
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

    if !apply_byte(document, edit.offset, edit.after) {
        return false;
    }

    document.undo_stack.push(edit);
    true
}

#[unsafe(no_mangle)]
pub extern "C" fn hm_file_document_save(handle: *mut FileDocumentHandle, path: *const c_char) -> bool {
    if handle.is_null() {
        return false;
    }

    let Some(target_path) = path_from_c(path) else {
        return false;
    };

    let document = unsafe { &mut *handle };

    if document.path == target_path {
        let mut temp_path = target_path.clone();
        temp_path.set_extension("hexmaster.tmp");
        if !save_document(document, &temp_path) {
            let _ = fs::remove_file(&temp_path);
            return false;
        }

        let _ = fs::remove_file(&target_path);
        if fs::rename(&temp_path, &target_path).is_err() {
            let _ = fs::remove_file(&temp_path);
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

    save_document(document, &target_path)
}
