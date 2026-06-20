use crate::vulkan_backend::swapchain::WindowRenderContext;
use ash::vk;
use objc2::{declare_class, msg_send, mutability, rc::Id, runtime::NSObject, DeclaredClass};
use objc2_foundation::NSRect;
use std::sync::Arc;

declare_class!(
    pub struct NSApplication;

    unsafe impl ClassType for NSApplication {
        type Super = NSObject;
        type Mutability = mutability::InteriorMutable;
        const NAME: &'static str = "NSApplication";
    }

    unsafe impl DeclaredClass for NSApplication {}

    unsafe impl NSApplication {
        #[method(sharedApplication)]
        pub fn shared_application() -> Option<Id<Self>> {
            unsafe { msg_send![class!(NSApplication), alloc] }
        }

        #[method(run)]
        pub fn run(&self) {
            loop {
                let event: Option<Id<NSObject>> = unsafe { msg_send![self, nextEventMatchingMask: usize::MAX untilDate: std::ptr::null::<NSObject>() inMode: std::ptr::null::<NSObject>() dequeue: true] };
                if let Some(ev) = event {
                    unsafe {
                        let _: () = msg_send![self, sendEvent: &ev];
                    }
                }
            }
        }
    }
);

declare_class!(
    pub struct NSWindow;

    unsafe impl ClassType for NSWindow {
        type Super = NSObject;
        type Mutability = mutability::InteriorMutable;
        const NAME: &'static str = "NSWindow";
    }

    unsafe impl DeclaredClass for NSWindow {}

    unsafe impl NSWindow {
        #[method(initWithContentRect:styleMask:backing:defer:)]
        pub fn init_with_content_rect(
            &mut self,
            rect: NSRect,
            _style_mask: usize,
            _backing: usize,
            _defer: bool,
        ) -> Option<Id<Self>> {
            let this: Option<Id<Self>> = unsafe { msg_send![super(this), init] };
            if this.is_some() {
                if let Some(shared) = unsafe { crate::frameworks::metal_sys::G_SHARED_DEVICE.as_ref() } {
                    let surface = vk::SurfaceKHR::null();
                    if let Ok(render_ctx) = WindowRenderContext::new(
                        Arc::clone(shared),
                        surface,
                        rect.size.width as u32,
                        rect.size.height as u32,
                        None,
                    ) {
                        crate::frameworks::quartz_core::set_global_render_context(render_ctx);
                    }
                }
                let view: Option<Id<NSView>> = unsafe { msg_send![class!(NSView), alloc] };
                if let Some(v) = view {
                    unsafe {
                        let _: Id<NSView> = msg_send![v, initWithFrame: rect];
                        let _: () = msg_send![this.as_ref().unwrap(), setContentView: &v];
                    }
                }
            }
            this
        }

        #[method(contentView)]
        pub fn content_view(&self) -> Option<Id<NSView>> {
            unsafe { msg_send![self, contentView] }
        }
    }
);

declare_class!(
    pub struct NSView;

    unsafe impl ClassType for NSView {
        type Super = NSObject;
        type Mutability = mutability::InteriorMutable;
        const NAME: &'static str = "NSView";
    }

    unsafe impl DeclaredClass for NSView {}

    unsafe impl NSView {
        #[method(initWithFrame:)]
        pub fn init_with_frame(&mut self, _frame: NSRect) -> Option<Id<Self>> {
            let this: Option<Id<Self>> = unsafe { msg_send![super(this), init] };
            if this.is_some() {
                let layer: Option<Id<NSObject>> = unsafe { msg_send![class!(CAMetalLayer), alloc] };
                if let Some(l) = layer {
                    unsafe {
                        let _: Id<NSObject> = msg_send![l, init];
                        let _: () = msg_send![this.as_ref().unwrap(), setLayer: &l];
                        let _: () = msg_send![this.as_ref().unwrap(), setWantsLayer: true];
                    }
                }
            }
            this
        }

        #[method(layer)]
        pub fn layer(&self) -> Option<Id<NSObject>> {
            unsafe { msg_send![self, layer] }
        }
    }
);
