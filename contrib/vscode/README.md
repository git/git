Configuration for VS Code
=========================

[VS Code](https://code.visualstudio.com/) is a lightweight but powerful source
code editor which runs on your desktop and is available for
[Windows](https://code.visualstudio.com/docs/setup/windows),
[macOS](https://code.visualstudio.com/docs/setup/mac) and
[Linux](https://code.visualstudio.com/docs/setup/linux). Among other languages,
it has [support for C/C++ via an extension](https://github.com/Microsoft/vscode-cpptools).

To start developing Git with VS Code, simply run the Unix shell script called
`init.sh` in this directory, which creates the configuration files in
`.vscode/` that VS Code consumes. `init.sh` needs access to `make` and `gcc`,
so run the script in a Git SDK shell if you are using Windows.
