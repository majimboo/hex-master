use hexapp_core::{ByteRange, Selection};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum TextEncoding {
    Ascii,
    Utf8,
    Utf16Le,
    Utf16Be,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum SearchPattern {
    HexBytes(Vec<u8>),
    Text {
        text: String,
        encoding: TextEncoding,
        case_sensitive: bool,
    },
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SearchQuery {
    pub pattern: SearchPattern,
    pub selection_only: Option<Selection>,
    pub wrap: bool,
    pub forward: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SearchHit {
    pub range: ByteRange,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SearchJobStatus {
    pub scanned_bytes: u64,
    pub total_bytes: u64,
    pub hits: usize,
    pub canceled: bool,
}
