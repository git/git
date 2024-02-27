@@
expression S, T, L;
@@
(
- strncmp(S, T, L) || S[L]
+ !!xstrncmpz(S, T, L)
|
- strncmp(S, T, L) || S[L] != '\0'
+ !!xstrncmpz(S, T, L)
|
- strncmp(S, T, L) || T[L]
+ !!xstrncmpz(T, S, L)
|
- strncmp(S, T, L) || T[L] != '\0'
+ !!xstrncmpz(T, S, L)
|
- !strncmp(S, T, L) && !S[L]
+ !xstrncmpz(S, T, L)
|
- !strncmp(S, T, L) && S[L] == '\0'
+ !xstrncmpz(S, T, L)
|
- !strncmp(S, T, L) && !T[L]
+ !xstrncmpz(T, S, L)
|
- !strncmp(S, T, L) && T[L] == '\0'
+ !xstrncmpz(T, S, L)
)
