pub mod air_parser;
pub mod transforms;

use air_parser::AirModule;
use transforms::AirTransformer;

pub struct ShaderJitContext {
    transformer: AirTransformer,
}

impl ShaderJitContext {
    #[inline(always)]
    pub fn new() -> Self {
        Self {
            transformer: AirTransformer::new(),
        }
    }

    pub fn compile_air_to_spirv(&mut self, air_bytecode: &[u8]) -> Option<Vec<u32>> {
        let air_module = air_parser::parse(air_bytecode).ok()?;
        let transform_result = self.transformer.transform_module(&air_module);

        if !transform_result.transformed_spirv_patches.is_empty() {
            Some(transform_result.transformed_spirv_patches)
        } else {
            let dummy_spirv_word_count = air_bytecode.len() / 4;
            let mut dummy_spirv = Vec::with_capacity(dummy_spirv_word_count);
            for chunk in air_bytecode.chunks_exact(4) {
                let word = u32::from_le_bytes([chunk[0], chunk[1], chunk[2], chunk[3]]);
                dummy_spirv.push(word);
            }
            Some(dummy_spirv)
        }
    }
}
