# Session 35 historical evidence tree digest

The original framing is documented in `docs/qualification/alpha-m1-session-38-manual-acquisition.md` (output-integrity section); Session 38's preflight record says the historical 149-file directory used the same procedure. The verifier at `alpha-m1-evidence-tree-digest.py` reproduces that framing and is fixed to the single historical directory.

For each regular file, depth-first through directories whose entry names are sorted by `os.fsencode`, the aggregate SHA-256 receives:

1. Relative path byte length as an unsigned 8-byte big-endian integer.
2. Relative path bytes (`/` between components, filesystem encoding with Python's filesystem error handling).
3. File size as an unsigned 8-byte big-endian integer.
4. Exact file contents.

The aggregate does not include a digest-of-digests, directory entries, permissions, owners, timestamps, or other metadata. The tool separately prints each file's SHA-256. It rejects symlinks and special files, safely opens the fixed root and children without following links, checks file identity/size/timestamps around reads, and rechecks each directory's names and identity/timestamps after walking it. It fails if the file count or aggregate digest differs from the historical expected values.

Run read-only with:

```sh
python3 -I qualification/alpha-m1-evidence-tree-digest.py
```

Do not pass a different root. This produces the 149 per-file hashes followed by the aggregate result. The procedure proves current local consistency with the recorded baseline under this framing; it is not an external signature and does not protect against a privileged actor who can alter the corpus and its reference values together. Empty directories and metadata are not covered by the historical framing.
