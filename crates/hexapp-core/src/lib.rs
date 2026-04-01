use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

pub type ByteOffset = u64;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct ByteRange {
    pub start: ByteOffset,
    pub len: ByteOffset,
}

impl ByteRange {
    pub fn end(self) -> ByteOffset {
        self.start.saturating_add(self.len)
    }

    pub fn contains(self, offset: ByteOffset) -> bool {
        offset >= self.start && offset < self.end()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct Caret {
    pub offset: ByteOffset,
    pub nibble: u8,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct Selection {
    pub anchor: ByteOffset,
    pub active: ByteOffset,
}

impl Selection {
    pub fn normalized(self) -> ByteRange {
        let start = self.anchor.min(self.active);
        let end = self.anchor.max(self.active);
        ByteRange {
            start,
            len: end.saturating_sub(start),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Bookmark {
    pub offset: ByteOffset,
    pub label: Option<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum EditMode {
    ReadOnly,
    Insert,
    Overwrite,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum DocumentKind {
    FileBacked,
    Scratch,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FileHandleInfo {
    pub path: PathBuf,
    pub read_only: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct DocumentSummary {
    pub id: u64,
    pub title: String,
    pub kind: DocumentKind,
    pub file: Option<FileHandleInfo>,
    pub len: ByteOffset,
    pub dirty: bool,
    pub mode: EditMode,
    pub selection: Option<Selection>,
    pub caret: Caret,
    pub bookmarks: Vec<Bookmark>,
}

impl DocumentSummary {
    pub fn empty_scratch(id: u64, title: impl Into<String>) -> Self {
        Self {
            id,
            title: title.into(),
            kind: DocumentKind::Scratch,
            file: None,
            len: 0,
            dirty: false,
            mode: EditMode::Insert,
            selection: None,
            caret: Caret {
                offset: 0,
                nibble: 0,
            },
            bookmarks: Vec::new(),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Document {
    summary: DocumentSummary,
}

impl Document {
    pub fn scratch(id: u64, title: impl Into<String>) -> Self {
        Self {
            summary: DocumentSummary::empty_scratch(id, title),
        }
    }

    pub fn file_backed(
        id: u64,
        path: impl AsRef<Path>,
        len: ByteOffset,
        read_only: bool,
    ) -> Self {
        let path = path.as_ref();
        let title = path
            .file_name()
            .and_then(|name| name.to_str())
            .unwrap_or("Untitled")
            .to_string();

        Self {
            summary: DocumentSummary {
                id,
                title,
                kind: DocumentKind::FileBacked,
                file: Some(FileHandleInfo {
                    path: path.to_path_buf(),
                    read_only,
                }),
                len,
                dirty: false,
                mode: if read_only {
                    EditMode::ReadOnly
                } else {
                    EditMode::Overwrite
                },
                selection: None,
                caret: Caret {
                    offset: 0,
                    nibble: 0,
                },
                bookmarks: Vec::new(),
            },
        }
    }

    pub fn summary(&self) -> &DocumentSummary {
        &self.summary
    }

    pub fn set_selection(&mut self, selection: Option<Selection>) {
        self.summary.selection = selection.map(|selection| Selection {
            anchor: clamp_offset(selection.anchor, self.summary.len),
            active: clamp_offset(selection.active, self.summary.len),
        });
    }

    pub fn set_caret(&mut self, caret: Caret) {
        self.summary.caret = Caret {
            offset: clamp_offset(caret.offset, self.summary.len),
            nibble: caret.nibble.min(1),
        };
    }

    pub fn add_bookmark(&mut self, bookmark: Bookmark) -> Result<(), CoreError> {
        if bookmark.offset > self.summary.len {
            return Err(CoreError::OffsetOutOfBounds {
                offset: bookmark.offset,
                len: self.summary.len,
            });
        }

        self.summary.bookmarks.push(bookmark);
        self.summary.bookmarks.sort_by_key(|bookmark| bookmark.offset);
        self.summary.bookmarks.dedup_by_key(|bookmark| bookmark.offset);
        Ok(())
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum EditCommand {
    Insert { at: ByteOffset, len: usize },
    Delete { range: ByteRange },
    Replace { range: ByteRange, len: usize },
    Fill { range: ByteRange, byte: u8 },
}

#[derive(Debug, thiserror::Error)]
pub enum CoreError {
    #[error("offset {offset} is out of bounds for document length {len}")]
    OffsetOutOfBounds { offset: ByteOffset, len: ByteOffset },
}

pub fn clamp_offset(offset: ByteOffset, len: ByteOffset) -> ByteOffset {
    offset.min(len)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn selection_is_normalized() {
        let selection = Selection {
            anchor: 20,
            active: 8,
        };

        assert_eq!(
            selection.normalized(),
            ByteRange {
                start: 8,
                len: 12,
            }
        );
    }

    #[test]
    fn file_backed_document_uses_file_name_as_title() {
        let document = Document::file_backed(7, Path::new("D:\\samples\\firmware.bin"), 4096, true);

        assert_eq!(document.summary().title, "firmware.bin");
        assert_eq!(document.summary().mode, EditMode::ReadOnly);
        assert_eq!(document.summary().len, 4096);
        assert!(document.summary().file.is_some());
    }
}
