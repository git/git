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

pub const GIT_MAX_RAWSZ: usize = 32;

/// A binary object ID.
#[repr(C)]
#[derive(Debug, Clone, Ord, PartialOrd, Eq, PartialEq)]
pub struct ObjectID {
    pub hash: [u8; GIT_MAX_RAWSZ],
    pub algo: u32,
}
