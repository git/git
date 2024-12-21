#ifndef SERVER_INFO_H
#define SERVER_INFO_H

struct repository;

/* Dumb servers support */
int update_server_info(struct repository *r, int force);

#endif /* SERVER_INFO_H */
