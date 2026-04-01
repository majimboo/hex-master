use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

use hexapp_core::{ByteOffset, ByteRange};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FileSourceMetadata {
    pub path: PathBuf,
    pub len: ByteOffset,
    pub read_only: bool,
}

pub trait ByteSource: Send + Sync {
    fn len(&self) -> ByteOffset;
    fn read_range(&self, range: ByteRange) -> Result<Vec<u8>, IoError>;
}

#[derive(Debug)]
pub struct FileByteSource {
    metadata: FileSourceMetadata,
}

impl FileByteSource {
    pub fn open(path: impl AsRef<Path>) -> Result<Self, IoError> {
        let path = path.as_ref().to_path_buf();
        let metadata = std::fs::metadata(&path)?;
        Ok(Self {
            metadata: FileSourceMetadata {
                path,
                len: metadata.len(),
                read_only: metadata.permissions().readonly(),
            },
        })
    }

    pub fn metadata(&self) -> &FileSourceMetadata {
        &self.metadata
    }
}

impl ByteSource for FileByteSource {
    fn len(&self) -> ByteOffset {
        self.metadata.len
    }

    fn read_range(&self, range: ByteRange) -> Result<Vec<u8>, IoError> {
        if range.end() > self.metadata.len {
            return Err(IoError::ReadOutOfBounds {
                start: range.start,
                len: range.len,
                file_len: self.metadata.len,
            });
        }

        let mut file = File::open(&self.metadata.path)?;
        file.seek(SeekFrom::Start(range.start))?;

        let mut buffer = vec![0_u8; range.len as usize];
        file.read_exact(&mut buffer)?;
        Ok(buffer)
    }
}

#[derive(Debug, thiserror::Error)]
pub enum IoError {
    #[error("i/o failure: {0}")]
    Io(#[from] std::io::Error),
    #[error("requested read [{start}, {len}] exceeds file length {file_len}")]
    ReadOutOfBounds {
        start: ByteOffset,
        len: ByteOffset,
        file_len: ByteOffset,
    },
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::{self, OpenOptions};
    use std::io::Write;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn unique_temp_path(name: &str) -> PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system time before epoch")
            .as_nanos();
        std::env::temp_dir().join(format!("hex_master_{name}_{nonce}.bin"))
    }

    #[test]
    fn reads_requested_range() {
        let path = unique_temp_path("range");
        {
            let mut file = File::create(&path).expect("create temp file");
            file.write_all(&[0x10, 0x20, 0x30, 0x40, 0x50])
                .expect("write test bytes");
        }

        let source = FileByteSource::open(&path).expect("open temp file");
        let bytes = source
            .read_range(ByteRange { start: 1, len: 3 })
            .expect("read range");

        assert_eq!(bytes, vec![0x20, 0x30, 0x40]);

        fs::remove_file(path).expect("remove temp file");
    }

    #[test]
    fn rejects_out_of_bounds_reads() {
        let path = unique_temp_path("oob");
        {
            let mut file = File::create(&path).expect("create temp file");
            file.write_all(&[0xAB, 0xCD]).expect("write test bytes");
        }

        let source = FileByteSource::open(&path).expect("open temp file");
        let error = source
            .read_range(ByteRange { start: 1, len: 4 })
            .expect_err("range should fail");

        match error {
            IoError::ReadOutOfBounds { file_len, .. } => assert_eq!(file_len, 2),
            other => panic!("unexpected error: {other:?}"),
        }

        fs::remove_file(path).expect("remove temp file");
    }

    #[test]
    fn supports_large_sparse_file_metadata() {
        let path = unique_temp_path("sparse");
        {
            let file = OpenOptions::new()
                .create(true)
                .truncate(true)
                .write(true)
                .open(&path)
                .expect("create temp file");
            file.set_len(5_u64 * 1024 * 1024 * 1024)
                .expect("set sparse file length");
        }

        let source = FileByteSource::open(&path).expect("open sparse file");
        assert_eq!(source.len(), 5_u64 * 1024 * 1024 * 1024);

        fs::remove_file(path).expect("remove temp file");
    }
}
