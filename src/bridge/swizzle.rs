use objc2::runtime::{Class, Sel};
use std::ffi::CStr;

extern "C" {
    fn class_replaceMethod(
        cls: *const Class,
        name: Sel,
        imp: unsafe extern "C" fn(),
        types: *const std::os::raw::c_char,
    ) -> unsafe extern "C" fn();
}

pub unsafe fn apply_swizzling() {
    let appkit = c"libvulkan_cocoa_core_bridge.dylib";
    
    if let Some(cls) = Class::get(c"NSWindow") {
        let sel = Sel::register(c"initWithContentRect:styleMask:backing:defer:");
        let custom_imp = crate::frameworks::appkit::NSWindow::init_with_content_rect 
            as *const () as unsafe extern "C" fn();
        class_replaceMethod(cls, sel, custom_imp, c"@@:{NSRect}QQB".as_ptr());
    }

    if let Some(cls) = Class::get(c"NSView") {
        let sel = Sel::register(c"initWithFrame:");
        let custom_imp = crate::frameworks::appkit::NSView::init_with_frame 
            as *const () as unsafe extern "C" fn();
        class_replaceMethod(cls, sel, custom_imp, c"@@:{NSRect}".as_ptr());
    }
}
