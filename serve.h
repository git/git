#ifndef SERVE_H
#define SERVE_H

struct serve_options {
	unsigned advertise_capabilities;
	unsigned stateless_rpc;
};
#define SERVE_OPTIONS_INIT { 0 }
void serve(struct serve_options *options);

#endif /* SERVE_H */
