#ifndef CREDENTIAL_H
#define CREDENTIAL_H

struct credential {
	char *description;
	char *username;
	char *password;
	char *unique;
};

struct string_list;

int credential_getpass(struct credential *);
void credential_from_config(struct credential *);

int credential_fill_gently(struct credential *, const struct string_list *methods);
void credential_fill(struct credential *, const struct string_list *methods);
void credential_reject(struct credential *, const struct string_list *methods);

int git_default_credential_config(const char *var, const char *value);

#endif /* CREDENTIAL_H */
