use std::os::raw::{c_char, c_void};
use std::ffi::CString;

#[repr(C)]
pub struct QmvIncomingFrame {
    pub client_token: u64,
    pub surface_port: u32,
    pub width: u32,
    pub height: u32,
    pub frame_index: u32,
    pub received_at_mach_time: u64,
}

#[link(name = "native_core", kind = "static")]
extern "C" {
    fn qmv_compositor_host_create(service_name: *const c_char) -> *mut c_void;
    fn qmv_compositor_host_set_frame_callback(
        host: *mut c_void,
        cb: extern "C" fn(*const QmvIncomingFrame, *mut c_void),
        user_data: *mut c_void,
    );
    fn qmv_compositor_host_start(host: *mut c_void) -> i32;
    fn qmv_compositor_host_destroy(host: *mut c_void);
    fn qmv_extract_fd_from_frame(frame: *const QmvIncomingFrame) -> i32;
}

extern "C" fn on_frame_received(frame_ptr: *const QmvIncomingFrame, _user_data: *mut c_void) {
    if frame_ptr.is_null() { return; }
    
    unsafe {
        let frame = &*frame_ptr;
        let fd = qmv_extract_fd_from_frame(frame_ptr);
        
        if fd >= 0 {
            println!(
                "[QMV Compositor] Received frame #{} from client {} ({}x{}). Extracted FD: {}", 
                frame.frame_index, frame.client_token, frame.width, frame.height, fd
            );
        }
    }
}

pub struct QmvServer {
    raw_host: *mut c_void,
}

impl QmvServer {
    pub fn start(service_name: &str) -> Self {
        let c_name = CString::new(service_name).unwrap();
        unsafe {
            let host = qmv_compositor_host_create(c_name.as_ptr());
            assert!(!host.is_null(), "Failed to initialize Mach IPC host");
            qmv_compositor_host_set_frame_callback(host, on_frame_received, std::ptr::null_mut());
            qmv_compositor_host_start(host);
            
            println!("[QMV Server] Display server started on service port: {}", service_name);
            QmvServer { raw_host: host }
        }
    }
}

impl Drop for QmvServer {
    fn drop(&mut self) {
        unsafe { qmv_compositor_host_destroy(self.raw_host); }
    }
}

}
