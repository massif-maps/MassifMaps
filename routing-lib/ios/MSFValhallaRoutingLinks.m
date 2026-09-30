// Carries -lz into the static library (clang auto-links a module's `link "z"`), since a binary
// Swift package target cannot declare the system libraries it needs.
@import zlib;
