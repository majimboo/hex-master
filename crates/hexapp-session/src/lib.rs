use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use time::OffsetDateTime;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RecentFile {
    pub path: PathBuf,
    pub opened_at: OffsetDateTime,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct WindowSettings {
    pub bytes_per_row: u16,
    pub font_family: String,
    pub restore_last_session: bool,
}

impl Default for WindowSettings {
    fn default() -> Self {
        Self {
            bytes_per_row: 16,
            font_family: "Consolas".to_string(),
            restore_last_session: true,
        }
    }
}
