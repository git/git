#ifndef COMPAT_WIN32_WSL_H
#define COMPAT_WIN32_WSL_H

int are_wsl_compatible_mode_bits_enabled(void);

int copy_wsl_mode_bits_from_disk(const wchar_t *wpath, ssize_t wpathlen,
				 _mode_t *mode);

int get_wsl_mode_bits_by_handle(HANDLE h, _mode_t *mode);
int set_wsl_mode_bits_by_handle(HANDLE h, _mode_t mode);

#endif
