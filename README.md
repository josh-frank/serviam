# `serviam`

`cc serviam.c -o serviam`

- No dependencies beyond POSIX
- Serves static files only (GET) from a directory (passed as `argv`)
- Fork per connection
- Path traversal protection
- Returns `[200, 404, 403, 405]`
- 5 hardcoded MIME types
- Handles GET only
- `index.html` fallback for directories