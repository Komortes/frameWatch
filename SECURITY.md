# Security Policy

## Scope

FrameWatch Mini is a local performance analysis tool. It uses DLL injection and a DX11 Present hook to attach to running processes on the local machine. Both the injector and the overlay DLL require elevated trust by design — they are not intended to be deployed in networked or multi-user environments.

## Reporting a Vulnerability

If you find a security issue (e.g. a path traversal in the settings file parser, unsafe handle usage in the injector, or a memory-safety problem in the overlay renderer), please report it privately rather than opening a public issue.

Open a [GitHub Security Advisory](../../security/advisories/new) or email the maintainer directly.

Please include:
- A description of the vulnerability and its impact
- Steps to reproduce or a proof-of-concept
- The affected component (injector, overlay DLL, core library, etc.)

## Out of scope

- Vulnerabilities that require an attacker to already have local administrator access on the machine
- Anti-cheat bypass techniques — the tool is not designed or supported for use with anti-cheat protected titles
- Denial-of-service against the host process via malformed settings files
