#ifndef FSMONITOR_H
#define FSMONITOR_H

int read_fsmonitor_extension(struct index_state *istate, const void *data, unsigned long sz);
void write_fsmonitor_extension(struct strbuf *sb, struct index_state *istate);
void tweak_fsmonitor_extension(struct index_state *istate);
void refresh_by_fsmonitor(struct index_state *istate);

#endif
