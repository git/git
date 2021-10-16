#ifndef SERVE_H
#define SERVE_H

void protocol_v2_advertise_capabilities(void);
void protocol_v2_serve_loop(int stateless_rpc);

#endif /* SERVE_H */
