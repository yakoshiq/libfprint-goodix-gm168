# Security policy

## Sensitive data

Do not attach any of the following to a public issue or commit:

- `psk.bin` or any decrypted/derived PSK material;
- `sealed.bin` or Windows DPAPI state;
- `calib_windows.dat`, `cal_scale.bin`, or FDT state;
- fingerprint images, templates, PGM/PPM/BMP files, or enrollment databases;
- USB/TLS captures, raw frames, session logs, VM images, firmware, or Windows
  driver binaries.

The repository `.gitignore` is only a last line of defense. Before every public
commit, review the exact staged file allowlist. Never rely on secret scanning to
recognize binary biometric or calibration data.

## Runtime storage

Production per-device state belongs in `/etc/goodix-gm168`:

```text
directory: root:root 0700
files:     root:root 0600
```

Driver protocol logging is disabled unless `GM168_LOG_DIR` is explicitly set.
Logs are created privately but must still be treated as sensitive.

## Reporting vulnerabilities

Use GitHub's private
[security-advisory form](https://github.com/yakoshiq/libfprint-goodix-gm168/security/advisories/new)
for vulnerability reports. Do not open a public issue containing exploit
details, device secrets, biometric data, or unredacted diagnostics. Start with
the minimum redacted information needed to establish impact and reproduction.

## Scope warning

This driver is reverse-engineered and validated on one device. It is not a
security-certified biometric authenticator, FIDO2 authenticator, or passkey
provider. PAM policy must always retain a password recovery path.
