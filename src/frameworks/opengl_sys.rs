use ash::vk;
use std::collections::HashMap;
use std::os::raw::{c_char, c_void, c_uint};
use std::sync::{Arc, Mutex};

pub const GL_ARRAY_BUFFER: c_uint = 0x8892;
pub const GL_ELEMENT_ARRAY_BUFFER: c_uint = 0x8893;
pub const GL_STATIC_DRAW: c_uint = 0x88E4;
pub const GL_DYNAMIC_DRAW: c_uint = 0x88E8;
pub const GL_TRIANGLES: c_uint = 0x0004;
pub const GL_UNSIGNED_INT: c_uint = 0x1405;

pub struct GlBuffer {
    pub buffer: vk::Buffer,
    pub memory: vk::DeviceMemory,
    pub size: vk::DeviceSize,
}

pub struct GlContext {
    pub buffers: HashMap<c_uint, GlBuffer>,
    pub bound_array_buffer: c_uint,
    pub bound_element_buffer: c_uint,
    pub next_id: c_uint,
}

impl GlContext {
    pub fn new() -> Self {
        Self {
            buffers: HashMap::new(),
            bound_array_buffer: 0,
            bound_element_buffer: 0,
            next_id: 1,
        }
    }
}

static mut GL_CONTEXT: Option<Mutex<GlContext>> = None;

#[inline(always)]
fn get_context() -> &'static Mutex<GlContext> {
    unsafe {
        if GL_CONTEXT.is_none() {
            GL_CONTEXT = Some(Mutex::new(GlContext::new()));
        }
        GL_CONTEXT.as_ref().unwrap()
    }
}

#[no_mangle]
pub extern "C" fn glGenBuffers(n: i32, buffers: *mut c_uint) {
    if n <= 0 || buffers.is_null() { return; }
    let shared = unsafe { crate::frameworks::metal_sys::G_SHARED_DEVICE.as_ref() };
    let shared = match shared {
        Some(s) => s,
        None => return,
    };
    let mut ctx = get_context().lock().unwrap();
    for i in 0..(n as usize) {
        let id = ctx.next_id;
        ctx.next_id += 1;
        unsafe { *buffers.add(i) = id; }
        ctx.buffers.insert(id, GlBuffer {
            buffer: vk::Buffer::null(),
            memory: vk::DeviceMemory::null(),
            size: 0,
        });
    }
}

#[no_mangle]
pub extern "C" fn glBindBuffer(target: c_uint, buffer: c_uint) {
    let mut ctx = get_context().lock().unwrap();
    match target {
        GL_ARRAY_BUFFER => ctx.bound_array_buffer = buffer,
        GL_ELEMENT_ARRAY_BUFFER => ctx.bound_element_buffer = buffer,
        _ => {}
    }
}

#[no_mangle]
pub extern "C" fn glBufferData(target: c_uint, size: isize, data: *const c_void, usage: c_uint) {
    if size <= 0 { return; }
    let shared = unsafe { crate::frameworks::metal_sys::G_SHARED_DEVICE.as_ref() };
    let shared = match shared {
        Some(s) => s,
        None => return,
    };
    let mut ctx = get_context().lock().unwrap();
    let current_id = match target {
        GL_ARRAY_BUFFER => ctx.bound_array_buffer,
        GL_ELEMENT_ARRAY_BUFFER => ctx.bound_element_buffer,
        _ => return,
    };
    if current_id == 0 { return; }
    let vk_usage = match target {
        GL_ARRAY_BUFFER => vk::BufferUsageFlags::VERTEX_BUFFER,
        GL_ELEMENT_ARRAY_BUFFER => vk::BufferUsageFlags::INDEX_BUFFER,
        _ => return,
    };
    if let Some(gl_buf) = ctx.buffers.get_mut(&current_id) {
        unsafe {
            if gl_buf.buffer != vk::Buffer::null() {
                shared.device.destroy_buffer(gl_buf.buffer, None);
                shared.device.free_memory(gl_buf.memory, None);
            }
        }
        let buffer_info = vk::BufferCreateInfo::default()
            .size(size as vk::DeviceSize)
            .usage(vk_usage | vk::BufferUsageFlags::TRANSFER_DST)
            .sharing_mode(vk::SharingMode::EXCLUSIVE);
        let buffer = unsafe { shared.device.create_buffer(&buffer_info, None).unwrap() };
        let mem_reqs = unsafe { shared.device.get_buffer_memory_requirements(buffer) };
        let mem_props = unsafe { shared.instance.get_physical_device_memory_properties(shared.physical_device) };
        let mut mem_type_index = 0;
        for i in 0..mem_props.memory_type_count {
            if (mem_reqs.memory_type_bits & (1 << i)) != 0
                && mem_props.memory_types[i as usize].property_flags.contains(vk::MemoryPropertyFlags::HOST_VISIBLE | vk::MemoryPropertyFlags::HOST_COHERENT)
            {
                mem_type_index = i;
                break;
            }
        }
        let alloc_info = vk::MemoryAllocateInfo::default()
            .allocation_size(mem_reqs.size)
            .memory_type_index(mem_type_index);
        let memory = unsafe { shared.device.allocate_memory(&alloc_info, None).unwrap() };
        unsafe {
            shared.device.bind_buffer_memory(buffer, memory, 0).unwrap();
            if !data.is_null() {
                let ptr = shared.device.map_memory(memory, 0, size as vk::DeviceSize, vk::MemoryMapFlags::empty()).unwrap();
                std::ptr::copy_nonoverlapping(data, ptr, size as usize);
                shared.device.unmap_memory(memory);
            }
        }
        gl_buf.buffer = buffer;
        gl_buf.memory = memory;
        gl_buf.size = size as vk::DeviceSize;
    }
}

#[no_mangle]
pub extern "C" fn glBufferSubData(target: c_uint, offset: isize, size: isize, data: *const c_void) {
    if size <= 0 || data.is_null() { return; }
    let shared = unsafe { crate::frameworks::metal_sys::G_SHARED_DEVICE.as_ref() };
    let shared = match shared {
        Some(s) => s,
        None => return,
    };
    let ctx = get_context().lock().unwrap();
    let current_id = match target {
        GL_ARRAY_BUFFER => ctx.bound_array_buffer,
        GL_ELEMENT_ARRAY_BUFFER => ctx.bound_element_buffer,
        _ => return,
    };
    if current_id == 0 { return; }
    if let Some(gl_buf) = ctx.buffers.get(&current_id) {
        if gl_buf.memory != vk::DeviceMemory::null() {
            unsafe {
                let ptr = shared.device.map_memory(gl_buf.memory, offset as vk::DeviceSize, size as vk::DeviceSize, vk::MemoryMapFlags::empty()).unwrap();
                std::ptr::copy_nonoverlapping(data, ptr, size as usize);
                shared.device.unmap_memory(gl_buf.memory);
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn glDeleteBuffers(n: i32, buffers: *const c_uint) {
    if n <= 0 || buffers.is_null() { return; }
    let shared = unsafe { crate::frameworks::metal_sys::G_SHARED_DEVICE.as_ref() };
    let shared = match shared {
        Some(s) => s,
        None => return,
    };
    let mut ctx = get_context().lock().unwrap();
    for i in 0..(n as usize) {
        let id = unsafe { *buffers.add(i) };
        if let Some(mut gl_buf) = ctx.buffers.remove(&id) {
            unsafe {
                if gl_buf.buffer != vk::Buffer::null() {
                    shared.device.destroy_buffer(gl_buf.buffer, None);
                    shared.device.free_memory(gl_buf.memory, None);
                }
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn glDrawElements(mode: c_uint, count: i32, type_enum: c_uint, indices: *const c_void) {
    if count <= 0 || mode != GL_TRIANGLES || type_enum != GL_UNSIGNED_INT { return; }
    let shared = unsafe { crate::frameworks::metal_sys::G_SHARED_DEVICE.as_ref() };
    let shared = match shared {
        Some(s) => s,
        None => return,
    };
    let ctx = get_context().lock().unwrap();
    let vbo_id = ctx.bound_array_buffer;
    let ibo_id = ctx.bound_element_buffer;
    if vbo_id == 0 || ibo_id == 0 { return; }
    if let (Some(vbo), Some(ibo)) = (ctx.buffers.get(&vbo_id), ctx.buffers.get(&ibo_id)) {
        if vbo.buffer != vk::Buffer::null() && ibo.buffer != vk::Buffer::null() {
            let cmd_buf = vk::CommandBuffer::null();
            let index_offset = indices as u64;
            unsafe {
                shared.device.cmd_bind_vertex_buffers(cmd_buf, 0, &[vbo.buffer], &[0]);
                shared.device.cmd_bind_index_buffer(cmd_buf, ibo.buffer, index_offset, vk::IndexType::UINT32);
                shared.device.cmd_draw_indexed(cmd_buf, count as u32, 1, 0, 0, 0);
            }
        }
    }
}
