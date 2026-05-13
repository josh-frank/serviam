# `serviam`

```
cc serviam.c -o serviam
./serviam 8080 /path/to/your/files
```
- **USELESS IN PROD**
- No dependencies beyond POSIX
- Serves static files only (`GET`) from a directory (passed as `argv`)
- Fork per connection
- Path traversal protection
- Returns `[200, 404, 403, 405]`
- 11 hardcoded MIME types
- `index.html` fallback for directories

## Why `serviam` is useless in prod

### No HTTPS
Serves plain HTTP, so everything in transit is plaintext: credentials,  session tokens and more — potentially readable by anyone between client and server.

### No rate limiting

Automated scanners? Serviam just answers them politely, every single time - no throttling, IP blocking, detection, nothing - Slowloris attack, SYN flooding, directory bruteforcing, all would wreck it.

### No logging worth anything
Serviam logs `192.168.1.1:54231 → connected` - no method, path, status code, timing, nothing - so you'd have no idea you were being attacked, what was being requested, or what succeeded.

### `fork()` per connection
Every connection spawns a whole process so any real load — let alone an attack — exhausts the process table

### No timeouts
Serviam waits patiently forever for slow clients, half-open connections, Slowloris attacks, anything. Bring it all down just by holding connections open until the server runs out of resources.

### No headers
- `Strict-Transport-Security` forces HTTPS
- `Content-Security-Policy` prevents injection attacks
- `X-Frame-Options` prevents clickjacking
- `X-Content-Type-Options` prevents MIME sniffing attacks

Serviam sends none of these.

### Static files only
No authentication, sessions, access control beyond UNIX permissions. Forget about anything requiring a login. If you accidentally left a `.env` file or `backup.zip` in the serve directory, serviam will hand it over immediately with a cheerful 200. No path filtering beyond the `..` check. No blocklist. No nothing.

## What real servers do
- Rate limiting by IP — those scanners get throttled and blocked
- Returns nothing useful for unknown paths — 404 with no information leakage
- Handles thousands of connections with event-driven I/O instead of forking
- Connection timeouts kill slow clients automatically
- Serves security headers
- TLS termination — handles HTTPS properly
- Access logs with full detail — method, path, status, bytes, timing, user agent
- Can block entire IP ranges with a single config line
- Integrates with fail2ban — automatic IP blocking after suspicious behavior
