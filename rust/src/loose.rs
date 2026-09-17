// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation: version 2 of the License, dated June 1991.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, see <https://www.gnu.org/licenses/>.

use crate::hash::{HashAlgorithm, ObjectID, GIT_MAX_RAWSZ};
use std::collections::BTreeMap;
use std::convert::TryInto;
use std::io::{self, Write};

/// The type of object stored in the map.
///
/// If this value is `Reserved`, then it is never written to disk and is used primarily to store
/// certain hard-coded objects, like the empty tree, empty blob, or null object ID.
///
/// If this value is `LooseObject`, then this represents a loose object.  `Shallow` represents a
/// shallow commit, its parent, or its tree.  `Submodule` represents a submodule commit.
#[repr(C)]
#[derive(Debug, Clone, Copy, Ord, PartialOrd, Eq, PartialEq)]
pub enum MapType {
    Reserved = 0,
    LooseObject = 1,
    Shallow = 2,
    Submodule = 3,
}

impl MapType {
    pub fn from_u32(n: u32) -> Option<MapType> {
        match n {
            0 => Some(Self::Reserved),
            1 => Some(Self::LooseObject),
            2 => Some(Self::Shallow),
            3 => Some(Self::Submodule),
            _ => None,
        }
    }
}

/// The value of an object stored in a `ObjectMemoryMap`.
///
/// This keeps the object ID to which the key is mapped and its kind together.
struct MappedObject {
    oid: ObjectID,
    kind: MapType,
}

/// Memory storage for a loose object.
struct ObjectMemoryMap {
    to_compat: BTreeMap<ObjectID, MappedObject>,
    to_storage: BTreeMap<ObjectID, MappedObject>,
    compat: HashAlgorithm,
    storage: HashAlgorithm,
}

impl ObjectMemoryMap {
    /// Create a new `ObjectMemoryMap`.
    ///
    /// The storage and compatibility `HashAlgorithm` instances are used to store the object IDs in
    /// the correct map.
    fn new(storage: HashAlgorithm, compat: HashAlgorithm) -> Self {
        Self {
            to_compat: BTreeMap::new(),
            to_storage: BTreeMap::new(),
            compat,
            storage,
        }
    }

    fn len(&self) -> usize {
        self.to_compat.len()
    }

    /// Write this map to an interface implementing `std::io::Write`.
    fn write<W: Write>(&self, wrtr: W) -> io::Result<()> {
        const VERSION_NUMBER: u32 = 1;
        const NUM_OBJECT_FORMATS: u32 = 2;
        const PADDING: [u8; 4] = [0u8; 4];

        let mut wrtr = wrtr;
        let header_size: u32 = (4 * 5) + (4 + 4 + 8) * NUM_OBJECT_FORMATS + 8;

        wrtr.write_all(b"LMAP")?;
        wrtr.write_all(&VERSION_NUMBER.to_be_bytes())?;
        wrtr.write_all(&header_size.to_be_bytes())?;
        wrtr.write_all(&(self.to_compat.len() as u32).to_be_bytes())?;
        wrtr.write_all(&NUM_OBJECT_FORMATS.to_be_bytes())?;

        let storage_short_len = self.find_short_name_len(&self.to_compat, self.storage);
        let compat_short_len = self.find_short_name_len(&self.to_storage, self.compat);

        let storage_npadding = Self::required_nul_padding(self.to_compat.len(), storage_short_len);
        let compat_npadding = Self::required_nul_padding(self.to_compat.len(), compat_short_len);

        let mut offset: u64 = header_size as u64;

        for (algo, len, npadding) in &[
            (self.storage, storage_short_len, storage_npadding),
            (self.compat, compat_short_len, compat_npadding),
        ] {
            wrtr.write_all(&algo.format_id().to_be_bytes())?;
            wrtr.write_all(&(*len as u32).to_be_bytes())?;

            offset += *npadding;
            wrtr.write_all(&offset.to_be_bytes())?;

            offset += self.to_compat.len() as u64 * (*len as u64 + algo.raw_len() as u64 + 4);
        }

        wrtr.write_all(&offset.to_be_bytes())?;

        let order_map: BTreeMap<&ObjectID, usize> = self
            .to_compat
            .keys()
            .enumerate()
            .map(|(i, oid)| (oid, i))
            .collect();

        wrtr.write_all(&PADDING[0..storage_npadding as usize])?;
        for oid in self.to_compat.keys() {
            wrtr.write_all(&oid.as_slice().unwrap()[0..storage_short_len])?;
        }
        for oid in self.to_compat.keys() {
            wrtr.write_all(oid.as_slice().unwrap())?;
        }
        for meta in self.to_compat.values() {
            wrtr.write_all(&(meta.kind as u32).to_be_bytes())?;
        }

        wrtr.write_all(&PADDING[0..compat_npadding as usize])?;
        for oid in self.to_storage.keys() {
            wrtr.write_all(&oid.as_slice().unwrap()[0..compat_short_len])?;
        }
        for meta in self.to_compat.values() {
            wrtr.write_all(meta.oid.as_slice().unwrap())?;
        }
        for meta in self.to_storage.values() {
            wrtr.write_all(&(order_map[&meta.oid] as u32).to_be_bytes())?;
        }

        Ok(())
    }

    fn required_nul_padding(nitems: usize, short_len: usize) -> u64 {
        let shortened_table_len = nitems as u64 * short_len as u64;
        let misalignment = shortened_table_len & 3;
        // If the value is 0, return 0; otherwise, return the difference from 4.
        (4 - misalignment) & 3
    }

    fn last_matching_offset(a: &ObjectID, b: &ObjectID, algop: HashAlgorithm) -> usize {
        for i in 0..=algop.raw_len() {
            if a.hash[i] != b.hash[i] {
                return i;
            }
        }
        algop.raw_len()
    }

    fn find_short_name_len(
        &self,
        map: &BTreeMap<ObjectID, MappedObject>,
        algop: HashAlgorithm,
    ) -> usize {
        if map.len() <= 1 {
            return 1;
        }
        let mut len = 1;
        let mut iter = map.keys();
        let mut cur = match iter.next() {
            Some(cur) => cur,
            None => return len,
        };
        for item in iter {
            let offset = Self::last_matching_offset(cur, item, algop);
            if offset >= len {
                len = offset + 1;
            }
            cur = item;
        }
        if len > algop.raw_len() {
            algop.raw_len()
        } else {
            len
        }
    }
}

struct ObjectFormatData {
    data_off: usize,
    shortened_len: usize,
    full_off: usize,
    mapping_off: Option<usize>,
}

pub struct MmapedObjectMapIter<'a> {
    offset: usize,
    algos: Vec<HashAlgorithm>,
    source: &'a MmapedObjectMap<'a>,
}

impl<'a> Iterator for MmapedObjectMapIter<'a> {
    type Item = Vec<ObjectID>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.offset >= self.source.nitems {
            return None;
        }
        let offset = self.offset;
        self.offset += 1;
        let v: Vec<ObjectID> = self
            .algos
            .iter()
            .cloned()
            .filter_map(|algo| self.source.oid_from_offset(offset, algo))
            .collect();
        if v.len() != self.algos.len() {
            return None;
        }
        Some(v)
    }
}

#[allow(dead_code)]
pub struct MmapedObjectMap<'a> {
    memory: &'a [u8],
    nitems: usize,
    meta_off: usize,
    obj_formats: BTreeMap<HashAlgorithm, ObjectFormatData>,
    main_algo: HashAlgorithm,
}

#[derive(Debug)]
#[allow(dead_code)]
enum MmapedParseError {
    HeaderTooSmall,
    InvalidSignature,
    InvalidVersion,
    UnknownAlgorithm,
    OffsetTooLarge,
    TooFewObjectFormats,
    UnalignedData,
    InvalidTrailerOffset,
}

#[allow(dead_code)]
impl<'a> MmapedObjectMap<'a> {
    fn new(
        slice: &'a [u8],
        hash_algo: HashAlgorithm,
    ) -> Result<MmapedObjectMap<'a>, MmapedParseError> {
        let object_format_header_size = 4 + 4 + 8;
        let trailer_offset_size = 8;
        let header_size: usize =
            4 + 4 + 4 + 4 + 4 + object_format_header_size * 2 + trailer_offset_size;
        if slice.len() < header_size {
            return Err(MmapedParseError::HeaderTooSmall);
        }
        if slice[0..4] != *b"LMAP" {
            return Err(MmapedParseError::InvalidSignature);
        }
        if Self::u32_at_offset(slice, 4) != 1 {
            return Err(MmapedParseError::InvalidVersion);
        }
        let _ = Self::u32_at_offset(slice, 8) as usize;
        let nitems = Self::u32_at_offset(slice, 12) as usize;
        let nobj_formats = Self::u32_at_offset(slice, 16) as usize;
        if nobj_formats < 2 {
            return Err(MmapedParseError::TooFewObjectFormats);
        }
        let mut offset = 20;
        let mut meta_off = None;
        let mut data = BTreeMap::new();
        for i in 0..nobj_formats {
            if offset + object_format_header_size + trailer_offset_size > slice.len() {
                return Err(MmapedParseError::HeaderTooSmall);
            }
            let format_id = Self::u32_at_offset(slice, offset);
            let shortened_len = Self::u32_at_offset(slice, offset + 4) as usize;
            let data_off = Self::u64_at_offset(slice, offset + 8);

            let algo = HashAlgorithm::from_format_id(format_id)
                .ok_or(MmapedParseError::UnknownAlgorithm)?;
            let data_off: usize = data_off
                .try_into()
                .map_err(|_| MmapedParseError::OffsetTooLarge)?;

            // Every object format must have these entries.
            let shortened_table_len = shortened_len
                .checked_mul(nitems)
                .ok_or(MmapedParseError::OffsetTooLarge)?;
            let full_off = data_off
                .checked_add(shortened_table_len)
                .ok_or(MmapedParseError::OffsetTooLarge)?;
            Self::verify_aligned(full_off)?;
            Self::verify_valid(slice, full_off as u64)?;

            let full_length = algo
                .raw_len()
                .checked_mul(nitems)
                .ok_or(MmapedParseError::OffsetTooLarge)?;
            let off = full_length
                .checked_add(full_off)
                .ok_or(MmapedParseError::OffsetTooLarge)?;
            Self::verify_aligned(off)?;
            Self::verify_valid(slice, off as u64)?;

            // This is for the metadata for the first object format and for the order mapping for
            // other object formats.
            let meta_size = nitems
                .checked_mul(4)
                .ok_or(MmapedParseError::OffsetTooLarge)?;
            let meta_end = off
                .checked_add(meta_size)
                .ok_or(MmapedParseError::OffsetTooLarge)?;
            Self::verify_valid(slice, meta_end as u64)?;

            let mut mapping_off = None;
            if i == 0 {
                meta_off = Some(off);
            } else {
                mapping_off = Some(off);
            }

            data.insert(
                algo,
                ObjectFormatData {
                    data_off,
                    shortened_len,
                    full_off,
                    mapping_off,
                },
            );
            offset += object_format_header_size;
        }
        let trailer = Self::u64_at_offset(slice, offset);
        Self::verify_aligned(trailer as usize)?;
        Self::verify_valid(slice, trailer)?;
        let end = trailer
            .checked_add(hash_algo.raw_len() as u64)
            .ok_or(MmapedParseError::OffsetTooLarge)?;
        if end != slice.len() as u64 {
            return Err(MmapedParseError::InvalidTrailerOffset);
        }
        match meta_off {
            Some(meta_off) => Ok(MmapedObjectMap {
                memory: slice,
                nitems,
                meta_off,
                obj_formats: data,
                main_algo: hash_algo,
            }),
            None => Err(MmapedParseError::TooFewObjectFormats),
        }
    }

    fn iter(&self) -> MmapedObjectMapIter<'_> {
        let mut algos = Vec::with_capacity(self.obj_formats.len());
        algos.push(self.main_algo);
        for algo in self.obj_formats.keys().cloned() {
            if algo != self.main_algo {
                algos.push(algo);
            }
        }
        MmapedObjectMapIter {
            offset: 0,
            algos,
            source: self,
        }
    }

    /// Treats `sl` as if it were a set of slices of `wanted.len()` bytes, and searches for
    /// `wanted` within it.
    ///
    /// If found, returns the offset of the subslice in `sl`.
    ///
    /// ```
    /// let sl = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
    ///
    /// assert_eq!(MmapedObjectMap::binary_search_slice(sl, &[2, 3]), Some(1));
    /// assert_eq!(MmapedObjectMap::binary_search_slice(sl, &[6, 7]), Some(4));
    /// assert_eq!(MmapedObjectMap::binary_search_slice(sl, &[1, 2]), None);
    /// assert_eq!(MmapedObjectMap::binary_search_slice(sl, &[10, 20]), None);
    /// ```
    fn binary_search_slice(sl: &[u8], wanted: &[u8]) -> Option<usize> {
        let len = wanted.len();
        let res = sl.binary_search_by(|item| {
            // We would like element_offset, but that is currently nightly only.  Instead, do a
            // pointer subtraction to find the index.
            let index = unsafe { (item as *const u8).offset_from(sl.as_ptr()) } as usize;
            // Now we have the index of this object.  Round it down to the nearest full-sized
            // chunk to find the actual offset where this starts.
            let index = index - (index % len);
            // Compute the comparison of that value instead, which will provide the expected
            // result.
            sl[index..index + wanted.len()].cmp(wanted)
        });
        res.ok().map(|offset| offset / len)
    }

    /// Look up `oid` in the map in order to convert it to `algo`.
    ///
    /// If this object is in the map, return the offset in the table for the main algorithm.
    fn look_up_object(&self, oid: &ObjectID) -> Option<usize> {
        let oid_algo = HashAlgorithm::from_u32(oid.algo)?;
        let params = self.obj_formats.get(&oid_algo)?;
        let short_table =
            &self.memory[params.data_off..params.data_off + (params.shortened_len * self.nitems)];
        let index = Self::binary_search_slice(
            short_table,
            &oid.as_slice().unwrap()[0..params.shortened_len],
        )?;
        match params.mapping_off {
            Some(from_off) => {
                // oid is in a compatibility algorithm.  Find the mapping index.
                let mapped = Self::u32_at_offset(self.memory, from_off + index * 4) as usize;
                if mapped >= self.nitems {
                    return None;
                }
                let oid_offset = params.full_off + mapped * oid_algo.raw_len();
                if self.memory[oid_offset..oid_offset + oid_algo.raw_len()]
                    != *oid.as_slice().unwrap()
                {
                    return None;
                }
                Some(mapped)
            }
            None => {
                // oid is in the main algorithm.  Find the object ID in the main map to confirm
                // it's correct.
                let oid_offset = params.full_off + index * oid_algo.raw_len();
                if self.memory[oid_offset..oid_offset + oid_algo.raw_len()]
                    != *oid.as_slice().unwrap()
                {
                    return None;
                }
                Some(index)
            }
        }
    }

    #[allow(dead_code)]
    fn map_object(&self, oid: &ObjectID, algo: HashAlgorithm) -> Option<MappedObject> {
        let main = self.look_up_object(oid)?;
        let meta = MapType::from_u32(Self::u32_at_offset(self.memory, self.meta_off + (main * 4)))?;
        Some(MappedObject {
            oid: self.oid_from_offset(main, algo)?,
            kind: meta,
        })
    }

    fn map_oid(&self, oid: &ObjectID, algo: HashAlgorithm) -> Option<ObjectID> {
        if algo as u32 == oid.algo {
            return Some(oid.clone());
        }

        let main = self.look_up_object(oid)?;
        self.oid_from_offset(main, algo)
    }

    fn oid_from_offset(&self, offset: usize, algo: HashAlgorithm) -> Option<ObjectID> {
        let aparams = self.obj_formats.get(&algo)?;

        let mut hash = [0u8; GIT_MAX_RAWSZ];
        let len = algo.raw_len();
        let oid_off = aparams.full_off + (offset * len);
        hash[0..len].copy_from_slice(&self.memory[oid_off..oid_off + len]);
        Some(ObjectID {
            hash,
            algo: algo as u32,
        })
    }

    fn u32_at_offset(slice: &[u8], offset: usize) -> u32 {
        u32::from_be_bytes(slice[offset..offset + 4].try_into().unwrap())
    }

    fn u64_at_offset(slice: &[u8], offset: usize) -> u64 {
        u64::from_be_bytes(slice[offset..offset + 8].try_into().unwrap())
    }

    fn verify_aligned(offset: usize) -> Result<(), MmapedParseError> {
        if (offset & 3) != 0 {
            return Err(MmapedParseError::UnalignedData);
        }
        Ok(())
    }

    fn verify_valid(slice: &[u8], offset: u64) -> Result<(), MmapedParseError> {
        if offset >= slice.len() as u64 {
            return Err(MmapedParseError::OffsetTooLarge);
        }
        Ok(())
    }
}

/// A map for loose and other non-packed object IDs that maps between a storage and compatibility
/// mapping.
///
/// In addition to the in-memory option, there is an optional batched storage, which can be used to
/// write objects to disk in an efficient way.
pub struct ObjectMap {
    mem: ObjectMemoryMap,
    batch: Option<ObjectMemoryMap>,
}

impl ObjectMap {
    /// Create a new `ObjectMap` with the given hash algorithms.
    ///
    /// This initializes the memory map to automatically map the empty tree, empty blob, and null
    /// object ID.
    pub fn new(storage: HashAlgorithm, compat: HashAlgorithm) -> Self {
        let mut map = ObjectMemoryMap::new(storage, compat);
        for (main, compat) in &[
            (storage.empty_tree(), compat.empty_tree()),
            (storage.empty_blob(), compat.empty_blob()),
            (storage.null_oid(), compat.null_oid()),
        ] {
            map.to_storage.insert(
                (*compat).clone(),
                MappedObject {
                    oid: (*main).clone(),
                    kind: MapType::Reserved,
                },
            );
            map.to_compat.insert(
                (*main).clone(),
                MappedObject {
                    oid: (*compat).clone(),
                    kind: MapType::Reserved,
                },
            );
        }
        Self {
            mem: map,
            batch: None,
        }
    }

    pub fn hash_algo(&self) -> HashAlgorithm {
        self.mem.storage
    }

    /// Start a batch for efficient writing.
    ///
    /// If there is already a batch started, this does nothing and the existing batch is retained.
    pub fn start_batch(&mut self) {
        if self.batch.is_none() {
            self.batch = Some(ObjectMemoryMap::new(self.mem.storage, self.mem.compat));
        }
    }

    pub fn batch_len(&self) -> Option<usize> {
        self.batch.as_ref().map(|b| b.len())
    }

    /// If a batch exists, write it to the writer.
    pub fn finish_batch<W: Write>(&mut self, w: W) -> io::Result<()> {
        if let Some(txn) = self.batch.take() {
            txn.write(w)?;
        }
        Ok(())
    }

    /// If a batch exists, write it to the writer.
    pub fn abort_batch(&mut self) {
        self.batch = None;
    }

    /// Return whether there is a batch already started.
    ///
    /// If you just want a batch to exist and don't care whether one has already been started, you
    /// may simply call `start_batch` unconditionally.
    pub fn has_batch(&self) -> bool {
        self.batch.is_some()
    }

    /// Insert an object into the map.
    ///
    /// If `write` is true and there is a batch started, write the object into the batch as well as
    /// into the memory map.
    pub fn insert(&mut self, oid1: &ObjectID, oid2: &ObjectID, kind: MapType, write: bool) {
        let (compat_oid, storage_oid) =
            if HashAlgorithm::from_u32(oid1.algo) == Some(self.mem.compat) {
                (oid1, oid2)
            } else {
                (oid2, oid1)
            };
        Self::insert_into(&mut self.mem, storage_oid, compat_oid, kind);
        if write {
            if let Some(ref mut batch) = self.batch {
                Self::insert_into(batch, storage_oid, compat_oid, kind);
            }
        }
    }

    fn insert_into(
        map: &mut ObjectMemoryMap,
        storage: &ObjectID,
        compat: &ObjectID,
        kind: MapType,
    ) {
        map.to_compat.insert(
            storage.clone(),
            MappedObject {
                oid: compat.clone(),
                kind,
            },
        );
        map.to_storage.insert(
            compat.clone(),
            MappedObject {
                oid: storage.clone(),
                kind,
            },
        );
    }

    #[allow(dead_code)]
    fn map_object(&self, oid: &ObjectID, algo: HashAlgorithm) -> Option<&MappedObject> {
        let map = if algo == self.mem.storage {
            &self.mem.to_storage
        } else {
            &self.mem.to_compat
        };
        map.get(oid)
    }

    #[allow(dead_code)]
    fn map_oid<'a, 'b: 'a>(
        &'b self,
        oid: &'a ObjectID,
        algo: HashAlgorithm,
    ) -> Option<&'a ObjectID> {
        if algo as u32 == oid.algo {
            return Some(oid);
        }
        let entry = self.map_object(oid, algo);
        entry.map(|obj| &obj.oid)
    }
}

#[cfg(test)]
mod tests {
    use super::{MapType, MmapedObjectMap, ObjectMap, ObjectMemoryMap};
    use crate::hash::{CryptoDigest, CryptoHasher, HashAlgorithm, ObjectID};
    use std::convert::TryInto;
    use std::io::{self, Cursor, Write};

    struct TrailingWriter {
        curs: Cursor<Vec<u8>>,
        hasher: CryptoHasher,
    }

    impl TrailingWriter {
        fn new() -> Self {
            Self {
                curs: Cursor::new(Vec::new()),
                hasher: CryptoHasher::new(HashAlgorithm::SHA256),
            }
        }

        fn finalize(mut self) -> Vec<u8> {
            let _ = self.hasher.flush();
            let mut v = self.curs.into_inner();
            v.extend(self.hasher.into_vec());
            v
        }
    }

    impl Write for TrailingWriter {
        fn write(&mut self, data: &[u8]) -> io::Result<usize> {
            self.hasher.write_all(data)?;
            self.curs.write_all(data)?;
            Ok(data.len())
        }

        fn flush(&mut self) -> io::Result<()> {
            self.hasher.flush()?;
            self.curs.flush()?;
            Ok(())
        }
    }

    fn sha1_oid(b: &[u8]) -> ObjectID {
        assert_eq!(b.len(), 20);
        let mut data = [0u8; 32];
        data[0..20].copy_from_slice(b);
        ObjectID {
            hash: data,
            algo: HashAlgorithm::SHA1 as u32,
        }
    }

    fn sha256_oid(b: &[u8]) -> ObjectID {
        assert_eq!(b.len(), 32);
        ObjectID {
            hash: b.try_into().unwrap(),
            algo: HashAlgorithm::SHA256 as u32,
        }
    }

    #[allow(clippy::type_complexity)]
    fn test_entries() -> &'static [(&'static str, &'static [u8], &'static [u8], MapType, bool)] {
        // These are all example blobs containing the content in the first argument.
        &[
            ("abc", b"\xf2\xba\x8f\x84\xab\x5c\x1b\xce\x84\xa7\xb4\x41\xcb\x19\x59\xcf\xc7\x09\x3b\x7f", b"\xc1\xcf\x6e\x46\x50\x77\x93\x0e\x88\xdc\x51\x36\x64\x1d\x40\x2f\x72\xa2\x29\xdd\xd9\x96\xf6\x27\xd6\x0e\x96\x39\xea\xba\x35\xa6", MapType::LooseObject, false),
            ("def", b"\x0c\x00\x38\x32\xe7\xbf\xa9\xca\x8b\x5c\x20\x35\xc9\xbd\x68\x4a\x5f\x26\x23\xbc", b"\x8a\x90\x17\x26\x48\x4d\xb0\xf2\x27\x9f\x30\x8d\x58\x96\xd9\x6b\xf6\x3a\xd6\xde\x95\x7c\xa3\x8a\xdc\x33\x61\x68\x03\x6e\xf6\x63", MapType::Shallow, true),
            ("ghi", b"\x45\xa8\x2e\x29\x5c\x52\x47\x31\x14\xc5\x7c\x18\xf4\xf5\x23\x68\xdf\x2a\x3c\xfd", b"\x6e\x47\x4c\x74\xf5\xd7\x78\x14\xc7\xf7\xf0\x7c\x37\x80\x07\x90\x53\x42\xaf\x42\x81\xe6\x86\x8d\x33\x46\x45\x4b\xb8\x63\xab\xc3", MapType::Submodule, false),
            ("jkl", b"\x45\x32\x8c\x36\xff\x2e\x9b\x9b\x4e\x59\x2c\x84\x7d\x3f\x9a\x7f\xd9\xb3\xe7\x16", b"\xc3\xee\xf7\x54\xa2\x1e\xc6\x9d\x43\x75\xbe\x6f\x18\x47\x89\xa8\x11\x6f\xd9\x66\xfc\x67\xdc\x31\xd2\x11\x15\x42\xc8\xd5\xa0\xaf", MapType::LooseObject, true),
        ]
    }

    fn test_map(write_all: bool) -> Box<ObjectMap> {
        let mut map = Box::new(ObjectMap::new(HashAlgorithm::SHA256, HashAlgorithm::SHA1));

        map.start_batch();

        for (_blob_content, sha1, sha256, kind, swap) in test_entries() {
            let s256 = sha256_oid(sha256);
            let s1 = sha1_oid(sha1);
            let write = write_all || (*kind as u32 & 2) == 0;
            if *swap {
                // Insert the item into the batch arbitrarily based on the type.  This tests that
                // we can specify either order and we'll do the right thing.
                map.insert(&s256, &s1, *kind, write);
            } else {
                map.insert(&s1, &s256, *kind, write);
            }
        }

        map
    }

    #[test]
    fn can_read_and_write_format() {
        for full in &[true, false] {
            let mut map = test_map(*full);
            let mut wrtr = TrailingWriter::new();
            map.finish_batch(&mut wrtr).unwrap();

            assert!(!map.has_batch());

            let data = wrtr.finalize();
            MmapedObjectMap::new(&data, HashAlgorithm::SHA256).unwrap();
        }
    }

    #[test]
    fn looks_up_from_mmaped() {
        let mut map = test_map(true);
        let mut wrtr = TrailingWriter::new();
        map.finish_batch(&mut wrtr).unwrap();

        assert!(!map.has_batch());

        let data = wrtr.finalize();
        let entries = test_entries();
        let map = MmapedObjectMap::new(&data, HashAlgorithm::SHA256).unwrap();

        for (_, sha1, sha256, kind, _) in entries {
            let s256 = sha256_oid(sha256);
            let s1 = sha1_oid(sha1);

            let res = map.map_object(&s256, HashAlgorithm::SHA1).unwrap();
            assert_eq!(res.oid, s1);
            assert_eq!(res.kind, *kind);
            let res = map.map_oid(&s256, HashAlgorithm::SHA1).unwrap();
            assert_eq!(res, s1);

            let res = map.map_object(&s256, HashAlgorithm::SHA256).unwrap();
            assert_eq!(res.oid, s256);
            assert_eq!(res.kind, *kind);
            let res = map.map_oid(&s256, HashAlgorithm::SHA256).unwrap();
            assert_eq!(res, s256);

            let res = map.map_object(&s1, HashAlgorithm::SHA256).unwrap();
            assert_eq!(res.oid, s256);
            assert_eq!(res.kind, *kind);
            let res = map.map_oid(&s1, HashAlgorithm::SHA256).unwrap();
            assert_eq!(res, s256);

            let res = map.map_object(&s1, HashAlgorithm::SHA1).unwrap();
            assert_eq!(res.oid, s1);
            assert_eq!(res.kind, *kind);
            let res = map.map_oid(&s1, HashAlgorithm::SHA1).unwrap();
            assert_eq!(res, s1);
        }

        for octet in &[0x00u8, 0x6d, 0x6e, 0x8a, 0xff] {
            let missing_oid = ObjectID {
                hash: [*octet; 32],
                algo: HashAlgorithm::SHA256 as u32,
            };

            assert!(map.map_object(&missing_oid, HashAlgorithm::SHA1).is_none());
            assert!(map.map_oid(&missing_oid, HashAlgorithm::SHA1).is_none());

            assert_eq!(
                map.map_oid(&missing_oid, HashAlgorithm::SHA256).unwrap(),
                missing_oid
            );
        }
    }

    #[test]
    fn binary_searches_slices_correctly() {
        let sl = &[
            0, 1, 2, 15, 14, 13, 18, 10, 2, 20, 20, 20, 21, 21, 0, 21, 21, 1, 21, 21, 21, 21, 21,
            22, 22, 23, 24,
        ];

        let expected: &[(&[u8], Option<usize>)] = &[
            (&[0, 1, 2], Some(0)),
            (&[15, 14, 13], Some(1)),
            (&[18, 10, 2], Some(2)),
            (&[20, 20, 20], Some(3)),
            (&[21, 21, 0], Some(4)),
            (&[21, 21, 1], Some(5)),
            (&[21, 21, 21], Some(6)),
            (&[21, 21, 22], Some(7)),
            (&[22, 23, 24], Some(8)),
            (&[2, 15, 14], None),
            (&[0, 21, 21], None),
            (&[21, 21, 23], None),
            (&[22, 22, 23], None),
            (&[0xff, 0xff, 0xff], None),
            (&[0, 0, 0], None),
        ];

        for (wanted, value) in expected {
            assert_eq!(MmapedObjectMap::binary_search_slice(sl, wanted), *value);
        }
    }

    #[test]
    fn looks_up_oid_correctly() {
        let map = test_map(false);
        let entries = test_entries();

        let s256 = sha256_oid(entries[0].2);
        let s1 = sha1_oid(entries[0].1);

        let missing_oid = ObjectID {
            hash: [0xffu8; 32],
            algo: HashAlgorithm::SHA256 as u32,
        };

        let res = map.map_object(&s256, HashAlgorithm::SHA1).unwrap();
        assert_eq!(res.oid, s1);
        assert_eq!(res.kind, MapType::LooseObject);
        let res = map.map_oid(&s256, HashAlgorithm::SHA1).unwrap();
        assert_eq!(*res, s1);

        let res = map.map_object(&s1, HashAlgorithm::SHA256).unwrap();
        assert_eq!(res.oid, s256);
        assert_eq!(res.kind, MapType::LooseObject);
        let res = map.map_oid(&s1, HashAlgorithm::SHA256).unwrap();
        assert_eq!(*res, s256);

        assert!(map.map_object(&missing_oid, HashAlgorithm::SHA1).is_none());
        assert!(map.map_oid(&missing_oid, HashAlgorithm::SHA1).is_none());

        assert_eq!(
            *map.map_oid(&missing_oid, HashAlgorithm::SHA256).unwrap(),
            missing_oid
        );
    }

    #[test]
    fn looks_up_known_oids_correctly() {
        let map = test_map(false);

        let funcs: &[&dyn Fn(HashAlgorithm) -> &'static ObjectID] = &[
            &|h: HashAlgorithm| h.empty_tree(),
            &|h: HashAlgorithm| h.empty_blob(),
            &|h: HashAlgorithm| h.null_oid(),
        ];

        for f in funcs {
            let s256 = f(HashAlgorithm::SHA256);
            let s1 = f(HashAlgorithm::SHA1);

            let res = map.map_object(s256, HashAlgorithm::SHA1).unwrap();
            assert_eq!(res.oid, *s1);
            assert_eq!(res.kind, MapType::Reserved);
            let res = map.map_oid(s256, HashAlgorithm::SHA1).unwrap();
            assert_eq!(*res, *s1);

            let res = map.map_object(s1, HashAlgorithm::SHA256).unwrap();
            assert_eq!(res.oid, *s256);
            assert_eq!(res.kind, MapType::Reserved);
            let res = map.map_oid(s1, HashAlgorithm::SHA256).unwrap();
            assert_eq!(*res, *s256);
        }
    }

    #[test]
    fn nul_padding() {
        assert_eq!(ObjectMemoryMap::required_nul_padding(1, 1), 3);
        assert_eq!(ObjectMemoryMap::required_nul_padding(2, 1), 2);
        assert_eq!(ObjectMemoryMap::required_nul_padding(3, 1), 1);
        assert_eq!(ObjectMemoryMap::required_nul_padding(2, 2), 0);

        assert_eq!(ObjectMemoryMap::required_nul_padding(39, 3), 3);
    }
}
