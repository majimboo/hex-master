use hexapp_ffi::AppState;

fn main() {
    let mut app_state = AppState::new();
    let document = app_state.create_scratch_document("Untitled");

    println!("Hex Master bootstrap");
    println!("Initial document: {} ({} bytes)", document.title, document.len);
    println!("Qt shell source: ui/qt-shell");
}
