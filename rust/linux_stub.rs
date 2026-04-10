use crate::{types::*, ComputerController};
use anyhow::Result;
use async_trait::async_trait;

pub struct LinuxController;

impl LinuxController {
    pub fn new() -> Result<Self> {
        Ok(Self)
    }
}

#[async_trait]
impl ComputerController for LinuxController {
    async fn take_screenshot(&self, _path: &str, _region: Option<Rect>, _window_id: Option<&str>) -> Result<()> {
        anyhow::bail!("Not implemented on Linux (Stub)");
    }

    async fn extract_text_from_screen(&self, _region: Rect, _window_id: &str) -> Result<String> {
         Ok("".to_string())
    }
    
    fn move_mouse(&self, _x: i32, _y: i32) -> Result<()> {
        Ok(())
    }
    
    fn click_at(&self, _x: i32, _y: i32, _app_name: Option<&str>) -> Result<()> {
        Ok(())
    }
    
    async fn extract_text_from_image(&self, _path: &str) -> Result<String> {
        Ok("".to_string())
    }
    
    async fn extract_text_with_locations(&self, _path: &str) -> Result<Vec<TextLocation>> {
        Ok(vec![])
    }
    
    async fn find_text_in_app(&self, _app_name: &str, _search_text: &str) -> Result<Option<TextLocation>> {
        Ok(None)
    }
}
