# dwl-state
Command-line tool to get dwl's state through the dwl-ipc protocol

# Dependencies
Make sure dwl has the [ipc](https://github.com/djpohly/dwl/wiki/ipc) patch applied before running. Also, make sure these packages are available.
 + make
 + wayland
 + wayland-protocols

## Compilation
Use `make` to compile, and `make install` to install, uninstall with `make uninstall`.

## Usage
There are nouns and verbs. Nouns determine what object your verbs will act on. Verbs determine what information you will get from the noun you choose.
By default if no nouns are provided but verbs are provided the active output and active tag are used (of course depending on the verb). If you don't want
any labels (the name of the noun) in the output use the `-n` flag.
