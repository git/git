#ifndef SERVE_H
#define SERVE_H

struct repository;

void protocol_v2_advertise_capabilities(struct repository *r);
void protocol_v2_serve_loop(struct repository *r, int stateless_rpc);

#endif /* SERVE_H */
