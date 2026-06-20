use std::collections::HashMap;
use thiserror::Error;

pub const BITCODE_WRAPPER_MAGIC: u32 = 0x0B17C0DE;
pub const RAW_BITCODE_MAGIC: [u8; 4] = [0x42, 0x43, 0xC0, 0xDE];

const END_BLOCK: u64 = 0;
const ENTER_SUBBLOCK: u64 = 1;
const DEFINE_ABBREV: u64 = 2;
const UNABBREV_RECORD: u64 = 3;
const FIRST_APPLICATION_ABBREV: u64 = 4;

const BLOCKINFO_BLOCK_ID: u64 = 0;
const BLOCKINFO_CODE_SETBID: u64 = 1;

#[derive(Error, Debug)]
pub enum AirParseError {
    #[error("input too short to contain a bitcode header")]
    TooShort,
    #[error("no recognizable LLVM bitcode magic (wrapper or raw) found")]
    NoBitcodeMagic,
    #[error("wrapper offset/size out of bounds of the input buffer")]
    WrapperOutOfBounds,
    #[error("malformed bitstream: {0}")]
    Malformed(&'static str),
    #[error("unexpected end of bitstream")]
    UnexpectedEof,
}

#[derive(Debug, Clone, Copy)]
pub struct BitcodeWrapperHeader {
    pub version: u32,
    pub offset: u32,
    pub size: u32,
    pub cpu_type: u32,
}

#[derive(Debug, Clone)]
pub enum DetectedAirFeature {
    KernelEntryPoint,
    VertexEntryPoint,
    FragmentEntryPoint,
    CompileOptions,
    LanguageVersion,
    ArgumentBufferUsage,
}

#[derive(Debug, Clone)]
pub struct BitstreamRecord {
    pub abbrev_id: u64,
    pub code: u64,
    pub operands: Vec<u64>,
    pub blob: Vec<u8>,
}

#[derive(Debug, Clone)]
pub struct BitstreamBlock {
    pub block_id: u64,
    pub abbrev_width: u32,
    pub records: Vec<BitstreamRecord>,
    pub subblocks: Vec<BitstreamBlock>,
}

#[derive(Debug, Clone)]
pub struct AirModule {
    pub wrapper: Option<BitcodeWrapperHeader>,
    pub detected_features: Vec<DetectedAirFeature>,
    pub blocks: Vec<BitstreamBlock>,
}

struct BitReader<'a> {
    data: &'a [u8],
    bit_pos: usize,
}

impl<'a> BitReader<'a> {
    fn new(data: &'a [u8]) -> Self {
        Self { data, bit_pos: 0 }
    }

    fn bits_remaining(&self) -> usize {
        self.data.len() * 8 - self.bit_pos
    }

    fn read(&mut self, num_bits: u32) -> Result<u64, AirParseError> {
        if num_bits == 0 {
            return Ok(0);
        }
        if num_bits as usize > self.bits_remaining() {
            return Err(AirParseError::UnexpectedEof);
        }
        let mut result: u64 = 0;
        for i in 0..num_bits {
            let byte_idx = self.bit_pos / 8;
            let bit_idx = self.bit_pos % 8;
            let bit = (self.data[byte_idx] >> bit_idx) & 1;
            result |= (bit as u64) << i;
            self.bit_pos += 1;
        }
        Ok(result)
    }

    fn read_vbr(&mut self, width: u32) -> Result<u64, AirParseError> {
        let hi_mask = 1u64 << (width - 1);
        let mut piece = self.read(width)?;
        if piece & hi_mask == 0 {
            return Ok(piece);
        }
        let mut result = piece & (hi_mask - 1);
        let mut shift = width - 1;
        loop {
            piece = self.read(width)?;
            result |= (piece & (hi_mask - 1)) << shift;
            if piece & hi_mask == 0 {
                break;
            }
            shift += width - 1;
        }
        Ok(result)
    }

    fn align32(&mut self) {
        let rem = self.bit_pos % 32;
        if rem != 0 {
            self.bit_pos += 32 - rem;
        }
    }
}

#[derive(Debug, Clone)]
enum AbbrevOp {
    Literal(u64),
    Fixed(u32),
    Vbr(u32),
    Array,
    Char6,
    Blob,
}

#[derive(Debug, Clone, Default)]
struct AbbrevDef {
    ops: Vec<AbbrevOp>,
}

#[derive(Default)]
struct AbbrevTable {
    blockinfo: HashMap<u64, Vec<AbbrevDef>>,
}

fn read_abbrev_def(reader: &mut BitReader) -> Result<AbbrevDef, AirParseError> {
    let num_ops = reader.read_vbr(5)?;
    let mut ops = Vec::with_capacity(num_ops as usize);
    for _ in 0..num_ops {
        let is_literal = reader.read(1)?;
        if is_literal != 0 {
            let value = reader.read_vbr(8)?;
            ops.push(AbbrevOp::Literal(value));
        } else {
            let encoding = reader.read(3)?;
            match encoding {
                1 => {
                    let w = reader.read_vbr(5)? as u32;
                    ops.push(AbbrevOp::Fixed(w));
                }
                2 => {
                    let w = reader.read_vbr(5)? as u32;
                    ops.push(AbbrevOp::Vbr(w));
                }
                3 => ops.push(AbbrevOp::Array),
                4 => ops.push(AbbrevOp::Char6),
                5 => ops.push(AbbrevOp::Blob),
                _ => return Err(AirParseError::Malformed("unknown abbrev operand encoding")),
            }
        }
    }
    Ok(AbbrevDef { ops })
}

fn read_abbreviated_record(
    reader: &mut BitReader,
    def: &AbbrevDef,
) -> Result<(u64, Vec<u64>, Vec<u8>), AirParseError> {
    let mut values = Vec::new();
    let mut blob = Vec::new();
    let mut i = 0;
    while i < def.ops.len() {
        match &def.ops[i] {
            AbbrevOp::Literal(v) => values.push(*v),
            AbbrevOp::Fixed(w) => values.push(reader.read(*w)?),
            AbbrevOp::Vbr(w) => values.push(reader.read_vbr(*w)?),
            AbbrevOp::Char6 => values.push(reader.read(6)?),
            AbbrevOp::Array => {
                let count = reader.read_vbr(6)?;
                i += 1;
                let elem_op = def
                    .ops
                    .get(i)
                    .ok_or(AirParseError::Malformed("array missing element type"))?;
                for _ in 0..count {
                    match elem_op {
                        AbbrevOp::Literal(v) => values.push(*v),
                        AbbrevOp::Fixed(w) => values.push(reader.read(*w)?),
                        AbbrevOp::Vbr(w) => values.push(reader.read_vbr(*w)?),
                        AbbrevOp::Char6 => values.push(reader.read(6)?),
                        _ => return Err(AirParseError::Malformed("invalid array element type")),
                    }
                }
            }
            AbbrevOp::Blob => {
                let len = reader.read_vbr(6)? as usize;
                reader.align32();
                blob.reserve(len);
                for _ in 0..len {
                    blob.push(reader.read(8)? as u8);
                }
                reader.align32();
            }
        }
        i += 1;
    }
    let code = values.first().copied().unwrap_or(0);
    let operands = if values.len() > 1 {
        values[1..].to_vec()
    } else {
        Vec::new()
    };
    Ok((code, operands, blob))
}

fn parse_block(
    reader: &mut BitReader,
    abbrev_width: u32,
    block_id: u64,
    table: &mut AbbrevTable,
) -> Result<BitstreamBlock, AirParseError> {
    let mut block = BitstreamBlock {
        block_id,
        abbrev_width,
        records: Vec::new(),
        subblocks: Vec::new(),
    };
    let mut local_abbrevs: Vec<AbbrevDef> = table.blockinfo.get(&block_id).cloned().unwrap_or_default();
    let mut current_setbid: Option<u64> = None;

    loop {
        if reader.bits_remaining() < abbrev_width as usize {
            return Err(AirParseError::UnexpectedEof);
        }
        let abbrev_id = reader.read(abbrev_width)?;

        match abbrev_id {
            END_BLOCK => {
                reader.align32();
                return Ok(block);
            }
            ENTER_SUBBLOCK => {
                let sub_block_id = reader.read_vbr(8)?;
                let new_abbrev_width = reader.read_vbr(4)? as u32;
                reader.align32();
                let _block_len_words = reader.read(32)?;
                let sub = parse_block(reader, new_abbrev_width, sub_block_id, table)?;
                block.subblocks.push(sub);
            }
            DEFINE_ABBREV => {
                let def = read_abbrev_def(reader)?;
                if block_id == BLOCKINFO_BLOCK_ID {
                    if let Some(target) = current_setbid {
                        table.blockinfo.entry(target).or_default().push(def);
                    }
                } else {
                    local_abbrevs.push(def);
                }
            }
            UNABBREV_RECORD => {
                let code = reader.read_vbr(6)?;
                let num_ops = reader.read_vbr(6)?;
                let mut operands = Vec::with_capacity(num_ops as usize);
                for _ in 0..num_ops {
                    operands.push(reader.read_vbr(6)?);
                }
                if block_id == BLOCKINFO_BLOCK_ID && code == BLOCKINFO_CODE_SETBID {
                    current_setbid = operands.first().copied();
                }
                block.records.push(BitstreamRecord {
                    abbrev_id,
                    code,
                    operands,
                    blob: Vec::new(),
                });
            }
            other => {
                let idx = (other - FIRST_APPLICATION_ABBREV) as usize;
                let def = local_abbrevs
                    .get(idx)
                    .ok_or(AirParseError::Malformed("abbreviated record references unknown abbreviation"))?
                    .clone();
                let (code, operands, blob) = read_abbreviated_record(reader, &def)?;
                block.records.push(BitstreamRecord {
                    abbrev_id,
                    code,
                    operands,
                    blob,
                });
            }
        }
    }
}

pub fn locate_bitstream(data: &[u8]) -> Result<(&[u8], Option<BitcodeWrapperHeader>), AirParseError> {
    if data.len() >= 4 && data[0..4] == RAW_BITCODE_MAGIC {
        return Ok((data, None));
    }
    if data.len() >= 20 {
        let magic = u32::from_le_bytes([data[0], data[1], data[2], data[3]]);
        if magic == BITCODE_WRAPPER_MAGIC {
            let version = u32::from_le_bytes([data[4], data[5], data[6], data[7]]);
            let offset = u32::from_le_bytes([data[8], data[9], data[10], data[11]]);
            let size = u32::from_le_bytes([data[12], data[13], data[14], data[15]]);
            let cpu_type = u32::from_le_bytes([data[16], data[17], data[18], data[19]]);

            let start = offset as usize;
            let end = start.checked_add(size as usize).ok_or(AirParseError::WrapperOutOfBounds)?;
            if end > data.len() {
                return Err(AirParseError::WrapperOutOfBounds);
            }
            let inner = &data[start..end];
            if inner.len() < 4 || inner[0..4] != RAW_BITCODE_MAGIC {
                return Err(AirParseError::NoBitcodeMagic);
            }
            return Ok((
                inner,
                Some(BitcodeWrapperHeader {
                    version,
                    offset,
                    size,
                    cpu_type,
                }),
            ));
        }
    }
    Err(AirParseError::NoBitcodeMagic)
}

fn scan_known_air_tags(body: &[u8]) -> Vec<DetectedAirFeature> {
    const TAGS: &[(&str, DetectedAirFeature)] = &[
        ("air.kernel", DetectedAirFeature::KernelEntryPoint),
        ("air.vertex", DetectedAirFeature::VertexEntryPoint),
        ("air.fragment", DetectedAirFeature::FragmentEntryPoint),
        ("air.compile_options", DetectedAirFeature::CompileOptions),
        ("air.version", DetectedAirFeature::LanguageVersion),
        ("air.argument_buffer", DetectedAirFeature::ArgumentBufferUsage),
    ];
    let haystack = String::from_utf8_lossy(body);
    TAGS.iter()
        .filter(|(tag, _)| haystack.contains(tag))
        .map(|(_, feature)| feature.clone())
        .collect()
}

pub fn parse(data: &[u8]) -> Result<AirModule, AirParseError> {
    if data.len() < 4 {
        return Err(AirParseError::TooShort);
    }
    let (bitstream, wrapper) = locate_bitstream(data)?;
    let body = &bitstream[4..];

    let mut reader = BitReader::new(body);
    let mut table = AbbrevTable::default();
    let mut top_level = Vec::new();

    while reader.bits_remaining() >= 32 {
        let abbrev_id = reader.read(2)?;
        if abbrev_id != ENTER_SUBBLOCK {
            break;
        }
        let block_id = reader.read_vbr(8)?;
        let new_width = reader.read_vbr(4)? as u32;
        reader.align32();
        let _len_words = reader.read(32)?;
        let block = parse_block(&mut reader, new_width, block_id, &mut table)?;
        top_level.push(block);
    }

    let detected_features = scan_known_air_tags(body);

    Ok(AirModule {
        wrapper,
        detected_features,
        blocks: top_level,
    })
}