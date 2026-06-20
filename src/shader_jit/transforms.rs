use crate::shader_jit::air_parser::{AirModule, BitstreamBlock, BitstreamRecord};
use std::collections::HashMap;

#[derive(Debug, Clone)]
pub struct BindingModifier {
    pub set: u32,
    pub binding: u32,
    pub is_bindless: bool,
    pub is_non_uniform: bool,
}

#[derive(Debug, Clone, Default)]
pub struct PipelineTransformResult {
    pub transformed_spirv_patches: Vec<u32>,
    pub binding_map: HashMap<String, BindingModifier>,
    pub requires_control_barrier_fix: bool,
}

pub struct AirTransformer {
    binding_counter: u32,
}

impl AirTransformer {
    #[inline(always)]
    pub fn new() -> Self {
        Self { binding_counter: 0 }
    }

    pub fn transform_module(&mut self, air: &AirModule) -> PipelineTransformResult {
        let mut result = PipelineTransformResult::default();
        let mut has_argument_buffers = false;

        for feature in &air.detected_features {
            match feature {
                crate::shader_jit::air_parser::DetectedAirFeature::ArgumentBufferUsage => {
                    has_argument_buffers = true;
                }
                _ => {}
            }
        }

        for block in &air.blocks {
            self.process_block_transforms(block, &mut result, has_argument_buffers);
        }

        result
    }

    fn process_block_transforms(
        &mut self,
        block: &BitstreamBlock,
        result: &mut PipelineTransformResult,
        has_ab: bool,
    ) {
        for record in &block.records {
            self.analyze_record_metadata(record, result, has_ab);
        }

        for sub_block in &block.subblocks {
            self.process_block_transforms(sub_block, result, has_ab);
        }
    }

    fn analyze_record_metadata(
        &mut self,
        record: &BitstreamRecord,
        result: &mut PipelineTransformResult,
        has_ab: bool,
    ) {
        if record.code == 5 || record.code == 6 {
            result.requires_control_barrier_fix = true;
        }

        if has_ab && !record.blob.is_empty() {
            if let Ok(potential_name) = String::from_utf8(record.blob.clone()) {
                if potential_name.contains("argument_buffer") || potential_name.contains("buffer_index") {
                    let current_binding = self.binding_counter;
                    self.binding_counter += 1;

                    result.binding_map.insert(
                        potential_name,
                        BindingModifier {
                            set: 0,
                            binding: current_binding,
                            is_bindless: true,
                            is_non_uniform: true,
                        },
                    );
                }
            }
        }
    }
}
