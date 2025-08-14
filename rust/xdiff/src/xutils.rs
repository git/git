use crate::*;
use xxhash_rust::xxh3::xxh3_64;

pub(crate) fn xdl_isspace(v: u8) -> bool {
    match v {
        b'\t' | b'\n' | b'\r' | b' ' => true,
        _ => false,
    }
}

pub struct WhitespaceIter<'a> {
    line: &'a [u8],
    index: usize,
    flags: u64,
}


impl<'a> WhitespaceIter<'a> {
    pub fn new(line: &'a [u8], flags: u64) -> Self {
        Self {
            line,
            index: 0,
            flags,
        }
    }
}

impl<'a> Iterator for WhitespaceIter<'a> {
    type Item = &'a [u8];

    fn next(&mut self) -> Option<Self::Item> {
        if self.index >= self.line.len() {
            return None;
        }

        // optimize case where --ignore-cr-at-eol is the only whitespace flag
        if (self.flags & XDF_WHITESPACE_FLAGS) == XDF_IGNORE_CR_AT_EOL {
            if self.index == 0 && self.line.ends_with(b"\r\n") {
                self.index = self.line.len() - 1;
                return Some(&self.line[..self.line.len() - 2])
            } else {
                let off = self.index;
                self.index = self.line.len();
                return Some(&self.line[off..])
            }
        }

        loop {
            let start = self.index;
            if self.index == self.line.len() {
                return None;
            }

            /* return contiguous run of not space bytes */
            while self.index < self.line.len() {
                if xdl_isspace(self.line[self.index]) {
                    break;
                }
                self.index += 1;
            }
            if self.index > start {
                return Some(&self.line[start..self.index]);
            }
            /* the current byte had better be a space */
            if !xdl_isspace(self.line[self.index]) {
                panic!("xdl_line_iter_next xdl_isspace() is false")
            }

            while self.index < self.line.len() && xdl_isspace(self.line[self.index]) {
                self.index += 1;
            }


            if self.index <= start {
                panic!("xdl_isspace() cannot simultaneously be true and false");
            }

            if (self.flags & XDF_IGNORE_WHITESPACE_AT_EOL) != 0
                && self.index == self.line.len()
            {
                return None;
            }
            if (self.flags & XDF_IGNORE_WHITESPACE) != 0 {
                continue;
            }
            if (self.flags & XDF_IGNORE_WHITESPACE_CHANGE) != 0 {
                if self.index == self.line.len() {
                    continue;
                }
                return Some(" ".as_bytes());
            }
            if (self.flags & XDF_IGNORE_CR_AT_EOL) != 0 {
                if start < self.line.len() && self.index == self.line.len() {
                    let mut end = self.line.len();
                    if end > 0 && self.line[end - 1] == b'\n' {
                        if end - start == 1 {
                            return Some(&self.line[start..end]);
                        } else {
                            end -= 1;
                        }
                        if end > 0 && self.line[end - 1] == b'\r' {
                            self.index = end;
                            end -= 1;
                            if end - start == 0 {
                                continue;
                            }
                            return Some(&self.line[start..end]);
                        }
                    }
                }
            }
            return Some(&self.line[start..self.index]);
        }
    }
}

pub fn chunked_iter_equal<'a, T, IT0, IT1>(mut it0: IT0, mut it1: IT1) -> bool
where
    T: Eq + 'a,
    IT0: Iterator<Item = &'a [T]>,
    IT1: Iterator<Item = &'a [T]>,
{
    let mut run_option0: Option<&[T]> = it0.next();
    let mut run_option1: Option<&[T]> = it1.next();
    let mut i0 = 0;
    let mut i1 = 0;

    while let (Some(run0), Some(run1)) = (run_option0, run_option1) {
        while i0 < run0.len() && i1 < run1.len() {
            if run0[i0] != run1[i1] {
                return false;
            }

            i0 += 1;
            i1 += 1;
        }

        if i0 == run0.len() {
            i0 = 0;
            run_option0 = it0.next();
        }
        if i1 == run1.len() {
            i1 = 0;
            run_option1 = it1.next();
        }
    }

    while let Some(run0) = run_option0 {
        if run0.len() == 0 {
            run_option0 = it0.next();
        } else {
            break;
        }
    }

    while let Some(run1) = run_option1 {
        if run1.len() == 0 {
            run_option1 = it1.next();
        } else {
            break;
        }
    }

    run_option0.is_none() && run_option1.is_none()
}


pub fn line_hash(line: &[u8], flags: u64) -> u64 {
    if (flags & XDF_WHITESPACE_FLAGS) == 0 {
        return xxh3_64(line);
    }

    let mut hasher = Xxh3Default::new();
    for chunk in WhitespaceIter::new(line, flags) {
        hasher.update(chunk);
    }

    hasher.finish()
}


pub fn line_equal(lhs: &[u8], rhs: &[u8], flags: u64) -> bool {
    if (flags & XDF_WHITESPACE_FLAGS) == 0 {
        return lhs == rhs;
    }

    // optimize case where --ignore-cr-at-eol is the only whitespace flag
    if (flags & XDF_WHITESPACE_FLAGS) == XDF_IGNORE_CR_AT_EOL {
        let a = lhs.ends_with(b"\r\n");
        let b = rhs.ends_with(b"\r\n");

        if !(a ^ b) {
            return lhs == rhs;
        } else {
            let lm = if a { 1 } else { 0 };
            let rm = if b { 1 } else { 0 };

            if lhs.len() - lm != rhs.len() - rm {
                return false;
            } else if &lhs[..lhs.len() - 1 - lm] != &rhs[..rhs.len() - 1 - rm] {
                return false;
            } else if lhs[lhs.len() - 1] != rhs[rhs.len() - 1] {
                return false;
            }
            return true;
        }
    }

    let lhs_it = WhitespaceIter::new(lhs, flags);
    let rhs_it = WhitespaceIter::new(rhs, flags);

    chunked_iter_equal(lhs_it, rhs_it)
}


#[cfg(test)]
mod tests {
    use crate::*;
    use crate::xutils::{chunked_iter_equal, WhitespaceIter};

    fn extract_string<'a>(line: &[u8], flags: u64, buffer: &'a mut Vec<u8>) -> &'a str {
        let it = WhitespaceIter::new(line, flags);
        buffer.clear();
        for run in it {
            #[cfg(test)]
            let _view = unsafe { std::str::from_utf8_unchecked(run) };
            buffer.extend_from_slice(run);
        }
        unsafe { std::str::from_utf8_unchecked(buffer.as_slice()) }
    }

    fn get_str_it<'a>(slice: &'a [&'a str]) -> impl Iterator<Item = &'a [u8]> + 'a {
        slice.iter().map(|v| (*v).as_bytes())
    }

    #[test]
    fn test_ignore_space() {
        let tv_individual = vec![
            ("ab\r", "ab\r", XDF_IGNORE_CR_AT_EOL),
            ("ab \r", "ab \r", XDF_IGNORE_CR_AT_EOL),
            ("\r \t a \r", "\r \t a \r", XDF_IGNORE_CR_AT_EOL),
            ("\r a \r", "\r a \r", XDF_IGNORE_CR_AT_EOL),
            ("\r", "\r", XDF_IGNORE_CR_AT_EOL),
            ("", "", XDF_IGNORE_CR_AT_EOL),
            ("\r a \r", "\r a \r", XDF_IGNORE_CR_AT_EOL),

            ("\r \t a \n", "\r \t a \r\n", XDF_IGNORE_CR_AT_EOL),
            ("\r a \n", "\r a \r\n", XDF_IGNORE_CR_AT_EOL),
            ("\n", "\r\n", XDF_IGNORE_CR_AT_EOL),
            ("\n", "\n", XDF_IGNORE_CR_AT_EOL),
            ("\r a \n", "\r a \n", XDF_IGNORE_CR_AT_EOL),

            ("1\n", "1\r\n", XDF_IGNORE_CR_AT_EOL),
            ("1", "1\r\n", XDF_IGNORE_WHITESPACE_CHANGE),

            ("\r \t a \r\n", "\r \t a \r\n", 0),
            ("\r a \r\n", "\r a \r\n", 0),
            ("\r\n", "\r\n", 0),
            ("\n", "\n", 0),
            ("\r a \n", "\r a \n", 0),
            ("     \n", "     \n", 0),
            ("a     \n", "a     \n", 0),
            ("  a  \t  asdf  \t \r\n", "  a  \t  asdf  \t \r\n", 0),
            ("\t a  b  \t \n", "\t a  b  \t \n", 0),
            ("  a b \t \r\n", "  a b \t \r\n", 0),
            ("\t  a \n", "\t  a \n", 0),
            ("\t\t\ta\t\n", "\t\t\ta\t\n", 0),
            ("a\n", "a\n", 0),
            ("\ta\n", "\ta\n", 0),

            ("a", "\r \t a \r\n", XDF_IGNORE_WHITESPACE),
            ("a", "\r a \r\n", XDF_IGNORE_WHITESPACE),
            ("", "\r\n", XDF_IGNORE_WHITESPACE),
            ("", "\n", XDF_IGNORE_WHITESPACE),
            ("a", "\r a \n", XDF_IGNORE_WHITESPACE),
            ("", "     \n", XDF_IGNORE_WHITESPACE),
            ("a", "a     \n", XDF_IGNORE_WHITESPACE),
            ("aasdf", "  a  \t  asdf  \t \r\n", XDF_IGNORE_WHITESPACE),
            ("ab", "\t a  b  \t \n", XDF_IGNORE_WHITESPACE),
            ("ab", "  a b \t \r\n", XDF_IGNORE_WHITESPACE),
            ("a", "\t  a \n", XDF_IGNORE_WHITESPACE),
            ("a", "\t\t\ta\t\n", XDF_IGNORE_WHITESPACE),
            ("a", "a\n", XDF_IGNORE_WHITESPACE),
            ("a", "\ta\n", XDF_IGNORE_WHITESPACE),

            ("", "     \n", XDF_IGNORE_WHITESPACE_AT_EOL),
            ("a", "a     \n", XDF_IGNORE_WHITESPACE_AT_EOL),
            ("  a  \t  asdf", "  a  \t  asdf  \t \r\n", XDF_IGNORE_WHITESPACE_AT_EOL),
            ("\t a  b", "\t a  b  \t \n", XDF_IGNORE_WHITESPACE_AT_EOL),

            (" a b", "  a b \t \r\n", XDF_IGNORE_WHITESPACE_CHANGE),
            (" a", "\t  a \n", XDF_IGNORE_WHITESPACE_CHANGE),
            (" a", "\t\t\ta\t\n", XDF_IGNORE_WHITESPACE_CHANGE),
            ("a", "a\n", XDF_IGNORE_WHITESPACE_CHANGE),
            (" a", "\ta\n", XDF_IGNORE_WHITESPACE_CHANGE),

            ("ab", "  a b \t \r\n", XDF_IGNORE_WHITESPACE | XDF_IGNORE_WHITESPACE_CHANGE),
            ("a", "\t  a \n", XDF_IGNORE_WHITESPACE | XDF_IGNORE_WHITESPACE_CHANGE),
            ("a", "\t\t\ta\t\n", XDF_IGNORE_WHITESPACE | XDF_IGNORE_WHITESPACE_CHANGE),
            ("a", "a\n", XDF_IGNORE_WHITESPACE | XDF_IGNORE_WHITESPACE_CHANGE),
            ("a", "\ta\n", XDF_IGNORE_WHITESPACE | XDF_IGNORE_WHITESPACE_CHANGE),
        ];

        let mut buffer = Vec::<u8>::new();
        for (expected, input, flags) in tv_individual {
            let actual = extract_string(input.as_bytes(), flags, &mut buffer);
            assert_eq!(expected, actual, "input: {:?} flags: 0x{:x}", input, flags);
        }
    }

    #[test]
    fn test_chunked_iter_equal() {
        let tv_str: Vec<(Vec<&str>, Vec<&str>)> = vec![
            /* equal cases */
            (vec!["", "", "abc"],         vec!["", "abc"]),
            (vec!["c", "", "a"],          vec!["c", "a"]),
            (vec!["a", "", "b", "", "c"], vec!["a", "b", "c"]),
            (vec!["", "", "a"],           vec!["a"]),
            (vec!["", "a"],               vec!["a"]),
            (vec![""],                    vec![]),
            (vec!["", ""],                vec![""]),
            (vec!["a"],                   vec!["", "", "a"]),
            (vec!["a"],                   vec!["", "a"]),
            (vec![],                      vec![""]),
            (vec![""],                    vec!["", ""]),
            (vec!["hello ", "world"],     vec!["hel", "lo wo", "rld"]),
            (vec!["hel", "lo wo", "rld"], vec!["hello ", "world"]),
            (vec!["hello world"],         vec!["hello world"]),
            (vec!["abc", "def"],          vec!["def", "abc"]),
            (vec![],                      vec![]),

            /* different cases */
            (vec!["abc"],       vec![]),
            (vec!["", "", ""],  vec!["", "a"]),
            (vec!["", "a"],     vec!["b", ""]),
            (vec!["abc"],       vec!["abc", "de"]),
            (vec!["abc", "de"], vec!["abc"]),
            (vec![],            vec!["a"]),
            (vec!["a"],         vec![]),
            (vec!["abc", "kj"], vec!["abc", "de"]),
        ];

        for (lhs, rhs) in tv_str.iter() {
            let a: Vec<u8> = get_str_it(lhs).flatten().copied().collect();
            let b: Vec<u8> = get_str_it(rhs).flatten().copied().collect();
            let expected = a.as_slice() == b.as_slice();

            let it0 = get_str_it(lhs);
            let it1 = get_str_it(rhs);
            let actual = chunked_iter_equal(it0, it1);
            assert_eq!(expected, actual);
        }
    }
}
