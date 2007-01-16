int spawnvppe_pipe(const char *cmd, const char **argv, const char **env, char **path, int pin[], int pout[]);
int spawnvpe_pipe(const char *cmd, const char **argv, const char **env, int pin[], int pout[]);
const char **copy_environ();
const char **copy_env(const char **env);
void env_unsetenv(const char **env, const char *name);
