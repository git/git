#ifndef SERVE_H
#define SERVE_H

struct strvec;
int has_capability(const struct strvec *keys, const char *capability,
		   const char **value);

struct serve_options {
	unsigned advertise_capabilities;
	unsigned stateless_rpc;
};
#define SERVE_OPTIONS_INIT { 0 }
void serve(struct serve_options *options);

#endif /* SERVE_H */
