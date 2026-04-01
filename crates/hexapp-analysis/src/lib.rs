use hexapp_core::ByteRange;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum HashAlgorithm {
    Crc32,
    Md5,
    Sha1,
    Sha256,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct HashRequest {
    pub algorithm: HashAlgorithm,
    pub range: Option<ByteRange>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct HashResult {
    pub algorithm: HashAlgorithm,
    pub hex_digest: String,
}
