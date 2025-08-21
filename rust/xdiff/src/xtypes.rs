use interop::ivec::IVec;

#[repr(C)]
pub(crate) struct xrecord {
    pub(crate) ptr: *const u8,
    pub(crate) size: usize,
    pub(crate) ha: u64,
}

#[repr(C)]
pub(crate) struct xdfile {
    pub(crate) record: IVec<xrecord>,
    pub(crate) dstart: isize,
    pub(crate) dend: isize,
    pub(crate) rchg: *mut u8,
    pub(crate) rindex: *mut usize,
    pub(crate) nreff: usize,
    pub(crate) ha: *mut u64,
}
