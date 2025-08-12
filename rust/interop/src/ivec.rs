use crate::*;
use core::mem::{align_of, size_of};
use std::fmt::{Debug, Formatter};
use std::ops::{Index, IndexMut};

#[repr(C)]
pub struct IVec<T> {
    ptr: *mut T,
    length: usize,
    capacity: usize,
    element_size: usize,
}

impl<T> Default for IVec<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> Drop for IVec<T> {
    fn drop(&mut self) {
        unsafe {
            self._free();
        }
    }
}

impl<T: Clone> Clone for IVec<T> {
    fn clone(&self) -> Self {
        let mut copy = Self::new();
        copy.reserve_exact(self.len());
        for i in 0..self.len() {
            copy.push(self[i].clone());
        }

        copy
    }
}

impl<T: PartialEq> PartialEq for IVec<T> {
    fn eq(&self, other: &Self) -> bool {
        if self.len() != other.len() {
            return false;
        }

        let lhs = self.as_slice();
        let rhs = &other.as_slice()[..lhs.len()];
        for i in 0..lhs.len() {
            if lhs[i] != rhs[i] {
                return false;
            }
        }

        true
    }
}

impl<T: PartialEq> Eq for IVec<T> {}

/*
 * constructors
 */
impl<T> IVec<T> {
    pub fn new() -> Self {
        Self {
            ptr: std::ptr::null_mut(),
            length: 0,
            capacity: 0,
            element_size: size_of::<T>(),
        }
    }

    /// uses calloc to create the IVec, it's unsafe because
    /// zeroed memory may not be a valid default value
    pub unsafe fn zero(capacity: usize) -> Self {
        Self {
            ptr: calloc(capacity, size_of::<T>()) as *mut T,
            length: capacity,
            capacity,
            element_size: size_of::<T>(),
        }
    }

    pub fn with_capacity(capacity: usize) -> Self {
        let mut vec = Self::new();
        vec._set_capacity(capacity);
        vec
    }

    pub fn with_capacity_and_default(capacity: usize, default_value: T) -> Self
    where
        T: Copy,
    {
        let mut vec = Self::new();
        vec._set_capacity(capacity);
        vec._buffer_mut().fill(default_value);
        vec
    }

    pub unsafe fn from_raw_mut<'a>(raw: *mut Self) -> &'a mut Self {
        let vec = raw.as_mut().expect("null pointer");
        #[cfg(debug_assertions)]
        vec.test_invariants();
        vec
    }

    pub unsafe fn from_raw<'a>(raw: *const Self) -> &'a Self {
        let vec = &*raw.as_ref().expect("null pointer");
        #[cfg(debug_assertions)]
        vec.test_invariants();
        vec
    }
}

/*
 * private methods
 */
impl<T> IVec<T> {
    pub fn test_invariants(&self) {
        if !self.ptr.is_null() && (self.ptr as usize) % align_of::<T>() != 0 {
            panic!(
                "misaligned pointer: expected {:x}, got {:x}",
                align_of::<T>(),
                self.ptr as usize
            );
        }
        if self.ptr.is_null() && (self.length > 0 || self.capacity > 0) {
            panic!("ptr is null, but length or capacity is > 0");
        }
        if !self.ptr.is_null() && self.capacity == 0 {
            panic!("ptr ISN'T null, but capacity == 0");
        }
        if self.element_size != size_of::<T>() {
            panic!(
                "incorrect element size, should be: {}, but was: {}",
                size_of::<T>(),
                self.element_size
            );
        }
        if self.length > self.capacity {
            panic!("length: {} > capacity: {}", self.length, self.capacity);
        }
        if self.capacity > usize::MAX / size_of::<T>() {
            panic!(
                "Capacity {} is too large, potential overflow detected",
                self.capacity
            );
        }
    }

    fn _zero(&mut self) {
        self.ptr = std::ptr::null_mut();
        self.length = 0;
        self.capacity = 0;
        // DO NOT MODIFY element_size!!!
    }

    unsafe fn _free(&mut self) {
        free(self.ptr as *mut std::ffi::c_void);
        self._zero();
    }

    fn _set_capacity(&mut self, new_capacity: usize) {
        unsafe {
            if new_capacity == self.capacity {
                return;
            }
            if new_capacity == 0 {
                self._free();
            } else {
                let t = realloc(
                    self.ptr as *mut std::ffi::c_void,
                    new_capacity * size_of::<T>(),
                );
                if t.is_null() {
                    panic!("out of memory");
                }
                self.ptr = t as *mut T;
            }
            self.capacity = new_capacity;
        }
    }

    fn _resize(&mut self, new_length: usize, default_value: T, exact: bool)
    where
        T: Copy,
    {
        if exact {
            self._set_capacity(new_length);
        } else if new_length > self.capacity {
            self.reserve(new_length - self.capacity);
        } else {
            /* capacity does not need to be changed */
        }

        if new_length > self.length {
            let range = self.length..new_length;
            self._buffer_mut()[range].fill(default_value);
        }

        self.length = new_length;
    }

    fn _buffer_mut(&mut self) -> &mut [T] {
        if self.ptr.is_null() {
            &mut []
        } else {
            unsafe { std::slice::from_raw_parts_mut(self.ptr, self.capacity) }
        }
    }

    fn _buffer(&self) -> &[T] {
        if self.ptr.is_null() {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(self.ptr, self.capacity) }
        }
    }
}

/*
 * methods
 */
impl<T> IVec<T> {
    pub fn len(&self) -> usize {
        self.length
    }

    pub unsafe fn set_len(&mut self, new_length: usize) {
        self.length = new_length;
    }

    pub fn capacity(&self) -> usize {
        self.capacity
    }

    pub fn reserve_exact(&mut self, additional: usize) {
        self._set_capacity(self.capacity + additional);
    }

    pub fn reserve(&mut self, additional: usize) {
        let growby = std::cmp::max(128, self.capacity);
        self.reserve_exact(std::cmp::max(additional, growby));
    }

    pub fn shrink_to_fit(&mut self) {
        self._set_capacity(self.length);
    }

    pub fn resize(&mut self, new_length: usize, default_value: T)
    where
        T: Copy,
    {
        self._resize(new_length, default_value, false);
    }

    pub fn resize_exact(&mut self, new_length: usize, default_value: T)
    where
        T: Copy,
    {
        self._resize(new_length, default_value, true);
    }

    pub fn insert(&mut self, index: usize, value: T) {
        if self.length == self.capacity {
            self.reserve(1);
        }

        unsafe {
            let src = &self._buffer()[index] as *const T;
            let dst = src.add(1) as *mut T;
            let len = self.length - index;
            std::ptr::copy(src, dst, len);
            std::ptr::write(self.ptr.add(index), value);
        }
    }

    pub fn push(&mut self, value: T) {
        if self.length == self.capacity {
            self.reserve(1);
        }

        let i = self.length;
        unsafe {
            std::ptr::write(self.ptr.add(i), value);
        }
        self.length += 1;
    }

    pub fn extend_from_slice(&mut self, slice: &[T])
    where
        T: Clone,
    {
        for v in slice {
            self.push(v.clone());
        }
    }

    pub fn clear(&mut self) {
        self.length = 0;
    }

    pub fn as_ptr(&self) -> *const T {
        self.ptr
    }

    pub fn as_mut_ptr(&self) -> *mut T {
        self.ptr
    }

    pub fn as_slice(&self) -> &[T] {
        &self._buffer()[0..self.length]
    }

    pub fn as_mut_slice(&mut self) -> &mut [T] {
        let range = 0..self.length;
        &mut self._buffer_mut()[range]
    }
}

impl<T> Extend<T> for IVec<T> {
    fn extend<IT: IntoIterator<Item = T>>(&mut self, iter: IT) {
        for v in iter {
            self.push(v);
        }
    }
}

impl<T> Index<usize> for IVec<T> {
    type Output = T;

    fn index(&self, index: usize) -> &Self::Output {
        &self.as_slice()[index]
    }
}

impl<T> IndexMut<usize> for IVec<T> {
    fn index_mut(&mut self, index: usize) -> &mut Self::Output {
        &mut self.as_mut_slice()[index]
    }
}

impl<T: Debug> Debug for IVec<T> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        writeln!(
            f,
            "ptr: {}, capacity: {}, len: {}, element_size: {}, content: {:?}",
            self.ptr as usize,
            self.capacity,
            self.length,
            self.element_size,
            self.as_slice()
        )
    }
}

impl std::fmt::Write for IVec<u8> {
    fn write_str(&mut self, s: &str) -> std::fmt::Result {
        Ok(self.extend_from_slice(s.as_bytes()))
    }
}

impl std::io::Write for IVec<u8> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.extend_from_slice(buf);
        Ok(buf.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use crate::ivec::IVec;
    use std::panic;

    #[test]
    fn test_panic_on_out_of_bounds() {
        type TestType = i16;
        let result = panic::catch_unwind(|| {
            let mut v = IVec::<TestType>::with_capacity(1_000_000);
            v[0] = 55;
        });

        match result {
            Ok(_) => assert!(false, "index was out of bounds, but no panic was triggered"),
            Err(_) => assert!(true),
        }
    }

    #[test]
    fn test_push_clear_resize_then_shrink_to_fit() {
        let mut vec = IVec::<u64>::new();
        let mut monotonic = 1;

        vec.reserve_exact(1);
        assert_eq!(1, vec.capacity);

        // test push
        for _ in 0..10 {
            vec.push(monotonic);
            assert_eq!(monotonic as usize, vec.length);
            assert_eq!(monotonic, vec[(monotonic - 1) as usize]);
            assert!(vec.capacity >= vec.length);
            monotonic += 1;
        }

        // test clear
        let expected = vec.capacity;
        vec.clear();
        assert_eq!(0, vec.length);
        assert_eq!(expected, vec.capacity);

        // test resize
        let expected = vec.capacity + 10;
        let default_value = 19;
        vec.resize(expected, default_value);
        // assert_eq!(vec.capacity, vec.slice.len());
        assert_eq!(expected, vec.length);
        assert!(vec.capacity >= expected);
        for i in 0..vec.length {
            assert_eq!(default_value, vec[i]);
        }

        vec.reserve(10);
        // assert_eq!(vec.capacity, vec.slice.len());
        assert!(vec.capacity > vec.length);
        let length_before = vec.length;
        vec.shrink_to_fit();
        assert_eq!(length_before, vec.length);
        assert_eq!(vec.length, vec.capacity);
        // assert_eq!(vec.capacity, vec.slice.len());
    }

    #[test]
    fn test_struct_size() {
        let vec = IVec::<i16>::new();

        assert_eq!(2, vec.element_size);
        assert_eq!(size_of::<usize>() * 4, size_of::<IVec<i16>>());

        drop(vec);

        let vec = IVec::<u128>::new();
        assert_eq!(16, vec.element_size);
        assert_eq!(size_of::<usize>() * 4, size_of::<IVec<u128>>());
    }

    #[test]
    fn test_manual_free() {
        type TestType = i16;
        let mut vec = IVec::<TestType>::new();

        unsafe { vec._free() };
        assert!(vec.ptr.is_null());
        assert_eq!(0, vec.length);
        assert_eq!(0, vec.capacity);
        assert_eq!(size_of::<TestType>(), vec.element_size);
    }
}
