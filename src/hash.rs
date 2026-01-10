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

use std::error::Error;
use std::fmt::{self, Debug, Display};
use std::io::{self, Write};
use std::os::raw::c_void;

pub const GIT_MAX_RAWSZ: usize = 32;

/// An error indicating an invalid hash algorithm.
///
/// The contained `u32` is the same as the `algo` field in `ObjectID`.
#[derive(Debug, Copy, Clone)]
pub struct InvalidHashAlgorithm(pub u32);

impl Display for InvalidHashAlgorithm {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "invalid hash algorithm {}", self.0)
    }
}

impl Error for InvalidHashAlgorithm {}

/// A binary object ID.
#[repr(C)]
#[derive(Clone, Ord, PartialOrd, Eq, PartialEq)]
pub struct ObjectID {
    pub hash: [u8; GIT_MAX_RAWSZ],
    pub algo: u32,
}

#[allow(dead_code)]
impl ObjectID {
    /// Return a new object ID with the given algorithm and hash.
    ///
    /// `hash` must be exactly the proper length for `algo` and this function panics if it is not.
    /// The extra internal storage of `hash`, if any, is zero filled.
    pub fn new(algo: HashAlgorithm, hash: &[u8]) -> Self {
        let mut data = [0u8; GIT_MAX_RAWSZ];
        // This verifies that the length of `hash` is correct.
        data[0..algo.raw_len()].copy_from_slice(hash);
        Self {
            hash: data,
            algo: algo as u32,
        }
    }

    /// Return the algorithm for this object ID.
    ///
    /// If the algorithm set internally is not valid, this function panics.
    pub fn algo(&self) -> Result<HashAlgorithm, InvalidHashAlgorithm> {
        HashAlgorithm::from_u32(self.algo).ok_or(InvalidHashAlgorithm(self.algo))
    }

    pub fn as_slice(&self) -> Result<&[u8], InvalidHashAlgorithm> {
        match HashAlgorithm::from_u32(self.algo) {
            Some(algo) => Ok(&self.hash[0..algo.raw_len()]),
            None => Err(InvalidHashAlgorithm(self.algo)),
        }
    }

    pub fn as_mut_slice(&mut self) -> Result<&mut [u8], InvalidHashAlgorithm> {
        match HashAlgorithm::from_u32(self.algo) {
            Some(algo) => Ok(&mut self.hash[0..algo.raw_len()]),
            None => Err(InvalidHashAlgorithm(self.algo)),
        }
    }
}

impl Display for ObjectID {
    /// Format this object ID as a hex object ID.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let hash = self.as_slice().unwrap();
        for x in hash {
            write!(f, "{:02x}", x)?;
        }
        Ok(())
    }
}

impl Debug for ObjectID {
    /// Format this object ID as a hex object ID with a colon and name appended to it.
    ///
    /// ```
    /// assert_eq!(
    ///     format!("{:?}", HashAlgorithm::SHA256.null_oid()),
    ///     "0000000000000000000000000000000000000000000000000000000000000000:sha256"
    /// );
    /// ```
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let hash = match self.as_slice() {
            Ok(hash) => hash,
            Err(_) => &self.hash,
        };
        for x in hash {
            write!(f, "{:02x}", x)?;
        }
        match self.algo() {
            Ok(algo) => write!(f, ":{}", algo.name()),
            Err(e) => write!(f, ":invalid-hash-algo-{}", e.0),
        }
    }
}

/// A trait to implement hashing with a cryptographic algorithm.
pub trait CryptoDigest {
    /// Return true if this digest is safe for use with untrusted data, false otherwise.
    fn is_safe(&self) -> bool;

    /// Update the digest with the specified data.
    fn update(&mut self, data: &[u8]);

    /// Return an object ID, consuming the hasher.
    fn into_oid(self) -> ObjectID;

    /// Return a hash as a `Vec`, consuming the hasher.
    fn into_vec(self) -> Vec<u8>;
}

/// A structure to hash data with a cryptographic hash algorithm.
///
/// Instances of this class are safe for use with untrusted data, provided Git has been compiled
/// with a collision-detecting implementation of SHA-1.
pub struct CryptoHasher {
    algo: HashAlgorithm,
    ctx: *mut c_void,
}

impl CryptoHasher {
    /// Create a new hasher with the algorithm specified with `algo`.
    ///
    /// This hasher is safe to use on untrusted data.  If SHA-1 is selected and Git was compiled
    /// with a collision-detecting implementation of SHA-1, then this function will use that
    /// implementation and detect any attempts at a collision.
    pub fn new(algo: HashAlgorithm) -> Self {
        let ctx = unsafe { c::git_hash_alloc() };
        unsafe { c::git_hash_init(ctx, algo.hash_algo_ptr()) };
        Self { algo, ctx }
    }
}

impl CryptoDigest for CryptoHasher {
    /// Return true if this digest is safe for use with untrusted data, false otherwise.
    fn is_safe(&self) -> bool {
        true
    }

    /// Update the hasher with the specified data.
    fn update(&mut self, data: &[u8]) {
        unsafe { c::git_hash_update(self.ctx, data.as_ptr() as *const c_void, data.len()) };
    }

    /// Return an object ID, consuming the hasher.
    fn into_oid(self) -> ObjectID {
        let mut oid = ObjectID {
            hash: [0u8; 32],
            algo: self.algo as u32,
        };
        unsafe { c::git_hash_final_oid(&mut oid as *mut ObjectID as *mut c_void, self.ctx) };
        oid
    }

    /// Return a hash as a `Vec`, consuming the hasher.
    fn into_vec(self) -> Vec<u8> {
        let mut v = vec![0u8; self.algo.raw_len()];
        unsafe { c::git_hash_final(v.as_mut_ptr(), self.ctx) };
        v
    }
}

impl Clone for CryptoHasher {
    fn clone(&self) -> Self {
        let ctx = unsafe { c::git_hash_alloc() };
        unsafe { c::git_hash_clone(ctx, self.ctx) };
        Self {
            algo: self.algo,
            ctx,
        }
    }
}

impl Drop for CryptoHasher {
    fn drop(&mut self) {
        unsafe { c::git_hash_free(self.ctx) };
    }
}

impl Write for CryptoHasher {
    fn write(&mut self, data: &[u8]) -> io::Result<usize> {
        self.update(data);
        Ok(data.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

/// A hash algorithm,
#[repr(C)]
#[derive(Debug, Copy, Clone, Ord, PartialOrd, Eq, PartialEq)]
pub enum HashAlgorithm {
    SHA1 = 1,
    SHA256 = 2,
}

#[allow(dead_code)]
impl HashAlgorithm {
    const SHA1_NULL_OID: ObjectID = ObjectID {
        hash: [0u8; 32],
        algo: Self::SHA1 as u32,
    };
    const SHA256_NULL_OID: ObjectID = ObjectID {
        hash: [0u8; 32],
        algo: Self::SHA256 as u32,
    };

    const SHA1_EMPTY_TREE: ObjectID = ObjectID {
        hash: *b"\x4b\x82\x5d\xc6\x42\xcb\x6e\xb9\xa0\x60\xe5\x4b\xf8\xd6\x92\x88\xfb\xee\x49\x04\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
        algo: Self::SHA1 as u32,
    };
    const SHA256_EMPTY_TREE: ObjectID = ObjectID {
        hash: *b"\x6e\xf1\x9b\x41\x22\x5c\x53\x69\xf1\xc1\x04\xd4\x5d\x8d\x85\xef\xa9\xb0\x57\xb5\x3b\x14\xb4\xb9\xb9\x39\xdd\x74\xde\xcc\x53\x21",
        algo: Self::SHA256 as u32,
    };

    const SHA1_EMPTY_BLOB: ObjectID = ObjectID {
        hash: *b"\xe6\x9d\xe2\x9b\xb2\xd1\xd6\x43\x4b\x8b\x29\xae\x77\x5a\xd8\xc2\xe4\x8c\x53\x91\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
        algo: Self::SHA1 as u32,
    };
    const SHA256_EMPTY_BLOB: ObjectID = ObjectID {
        hash: *b"\x47\x3a\x0f\x4c\x3b\xe8\xa9\x36\x81\xa2\x67\xe3\xb1\xe9\xa7\xdc\xda\x11\x85\x43\x6f\xe1\x41\xf7\x74\x91\x20\xa3\x03\x72\x18\x13",
        algo: Self::SHA256 as u32,
    };

    /// Return a hash algorithm based on the internal integer ID used by Git.
    ///
    /// Returns `None` if the algorithm doesn't indicate a valid algorithm.
    pub const fn from_u32(algo: u32) -> Option<HashAlgorithm> {
        match algo {
            1 => Some(HashAlgorithm::SHA1),
            2 => Some(HashAlgorithm::SHA256),
            _ => None,
        }
    }

    /// Return a hash algorithm based on the internal integer ID used by Git.
    ///
    /// Returns `None` if the algorithm doesn't indicate a valid algorithm.
    pub const fn from_format_id(algo: u32) -> Option<HashAlgorithm> {
        match algo {
            0x73686131 => Some(HashAlgorithm::SHA1),
            0x73323536 => Some(HashAlgorithm::SHA256),
            _ => None,
        }
    }

    /// The name of this hash algorithm as a string suitable for the configuration file.
    pub const fn name(self) -> &'static str {
        match self {
            HashAlgorithm::SHA1 => "sha1",
            HashAlgorithm::SHA256 => "sha256",
        }
    }

    /// The format ID of this algorithm for binary formats.
    ///
    /// Note that when writing this to a data format, it should be written in big-endian format
    /// explicitly.
    pub const fn format_id(self) -> u32 {
        match self {
            HashAlgorithm::SHA1 => 0x73686131,
            HashAlgorithm::SHA256 => 0x73323536,
        }
    }

    /// The length of binary object IDs in this algorithm in bytes.
    pub const fn raw_len(self) -> usize {
        match self {
            HashAlgorithm::SHA1 => 20,
            HashAlgorithm::SHA256 => 32,
        }
    }

    /// The length of object IDs in this algorithm in hexadecimal characters.
    pub const fn hex_len(self) -> usize {
        self.raw_len() * 2
    }

    /// The number of bytes which is processed by one iteration of this algorithm's compression
    /// function.
    pub const fn block_size(self) -> usize {
        match self {
            HashAlgorithm::SHA1 => 64,
            HashAlgorithm::SHA256 => 64,
        }
    }

    /// The object ID representing the empty blob.
    pub const fn empty_blob(self) -> &'static ObjectID {
        match self {
            HashAlgorithm::SHA1 => &Self::SHA1_EMPTY_BLOB,
            HashAlgorithm::SHA256 => &Self::SHA256_EMPTY_BLOB,
        }
    }

    /// The object ID representing the empty tree.
    pub const fn empty_tree(self) -> &'static ObjectID {
        match self {
            HashAlgorithm::SHA1 => &Self::SHA1_EMPTY_TREE,
            HashAlgorithm::SHA256 => &Self::SHA256_EMPTY_TREE,
        }
    }

    /// The object ID which is all zeros.
    pub const fn null_oid(self) -> &'static ObjectID {
        match self {
            HashAlgorithm::SHA1 => &Self::SHA1_NULL_OID,
            HashAlgorithm::SHA256 => &Self::SHA256_NULL_OID,
        }
    }

    /// A pointer to the C `struct git_hash_algo` for interoperability with C.
    pub fn hash_algo_ptr(self) -> *const c_void {
        unsafe { c::hash_algo_ptr_by_number(self as u32) }
    }

    /// Create a hasher for this algorithm.
    pub fn hasher(self) -> CryptoHasher {
        CryptoHasher::new(self)
    }
}

pub mod c {
    use std::os::raw::c_void;

    extern "C" {
        pub fn hash_algo_ptr_by_number(n: u32) -> *const c_void;
        pub fn unsafe_hash_algo(algop: *const c_void) -> *const c_void;
        pub fn git_hash_alloc() -> *mut c_void;
        pub fn git_hash_free(ctx: *mut c_void);
        pub fn git_hash_init(dst: *mut c_void, algop: *const c_void);
        pub fn git_hash_clone(dst: *mut c_void, src: *const c_void);
        pub fn git_hash_update(ctx: *mut c_void, inp: *const c_void, len: usize);
        pub fn git_hash_final(hash: *mut u8, ctx: *mut c_void);
        pub fn git_hash_final_oid(hash: *mut c_void, ctx: *mut c_void);
    }
}

#[cfg(test)]
mod tests {
    use super::{CryptoDigest, HashAlgorithm, ObjectID};
    use std::io::Write;

    fn all_algos() -> &'static [HashAlgorithm] {
        &[HashAlgorithm::SHA1, HashAlgorithm::SHA256]
    }

    #[test]
    fn format_id_round_trips() {
        for algo in all_algos() {
            assert_eq!(
                *algo,
                HashAlgorithm::from_format_id(algo.format_id()).unwrap()
            );
        }
    }

    #[test]
    fn offset_round_trips() {
        for algo in all_algos() {
            assert_eq!(*algo, HashAlgorithm::from_u32(*algo as u32).unwrap());
        }
    }

    #[test]
    fn slices_have_correct_length() {
        for algo in all_algos() {
            for oid in [algo.null_oid(), algo.empty_blob(), algo.empty_tree()] {
                assert_eq!(oid.as_slice().unwrap().len(), algo.raw_len());
            }
        }
    }

    #[test]
    fn object_ids_format_correctly() {
        let entries = &[
            (
                HashAlgorithm::SHA1.null_oid(),
                "0000000000000000000000000000000000000000",
                "0000000000000000000000000000000000000000:sha1",
            ),
            (
                HashAlgorithm::SHA1.empty_blob(),
                "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391",
                "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391:sha1",
            ),
            (
                HashAlgorithm::SHA1.empty_tree(),
                "4b825dc642cb6eb9a060e54bf8d69288fbee4904",
                "4b825dc642cb6eb9a060e54bf8d69288fbee4904:sha1",
            ),
            (
                HashAlgorithm::SHA256.null_oid(),
                "0000000000000000000000000000000000000000000000000000000000000000",
                "0000000000000000000000000000000000000000000000000000000000000000:sha256",
            ),
            (
                HashAlgorithm::SHA256.empty_blob(),
                "473a0f4c3be8a93681a267e3b1e9a7dcda1185436fe141f7749120a303721813",
                "473a0f4c3be8a93681a267e3b1e9a7dcda1185436fe141f7749120a303721813:sha256",
            ),
            (
                HashAlgorithm::SHA256.empty_tree(),
                "6ef19b41225c5369f1c104d45d8d85efa9b057b53b14b4b9b939dd74decc5321",
                "6ef19b41225c5369f1c104d45d8d85efa9b057b53b14b4b9b939dd74decc5321:sha256",
            ),
        ];
        for (oid, display, debug) in entries {
            assert_eq!(format!("{}", oid), *display);
            assert_eq!(format!("{:?}", oid), *debug);
        }
    }

    #[test]
    fn hasher_works_correctly() {
        for algo in all_algos() {
            let tests: &[(&[u8], &ObjectID)] = &[
                (b"blob 0\0", algo.empty_blob()),
                (b"tree 0\0", algo.empty_tree()),
            ];
            for (data, oid) in tests {
                let mut h = algo.hasher();
                assert!(h.is_safe());
                // Test that this works incrementally.
                h.update(&data[0..2]);
                h.update(&data[2..]);

                let h2 = h.clone();

                let actual_oid = h.into_oid();
                assert_eq!(**oid, actual_oid);

                let v = h2.into_vec();
                assert_eq!((*oid).as_slice().unwrap(), &v);

                let mut h = algo.hasher();
                h.write_all(&data[0..2]).unwrap();
                h.write_all(&data[2..]).unwrap();

                let actual_oid = h.into_oid();
                assert_eq!(**oid, actual_oid);
            }
        }
    }
}
