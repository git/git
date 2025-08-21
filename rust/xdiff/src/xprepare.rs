use crate::xtypes::xdfile;

///
/// Early trim initial and terminal matching records.
///
pub(crate) fn trim_ends(xdf1: &mut xdfile, xdf2: &mut xdfile) {
    let mut lim = std::cmp::min(xdf1.record.len(), xdf2.record.len());

    for i in 0..lim {
        if xdf1.record[i].ha != xdf2.record[i].ha {
            xdf1.dstart = i as isize;
            xdf2.dstart = i as isize;
            lim -= i;
            break;
        }
    }

    for i in 0..lim {
        let f1i = xdf1.record.len() - 1 - i;
        let f2i = xdf2.record.len() - 1 - i;
        if xdf1.record[f1i].ha != xdf2.record[f2i].ha {
            xdf1.dend = f1i as isize;
            xdf2.dend = f2i as isize;
            break;
        }
    }
}
