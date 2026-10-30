# Session 42 Core review

**Disposition: PASS.** Core independently compared the documented inline loader to the test constant: byte-identical, SHA-256 `feef002511128263daf5fa05fcaa72686a688d58fb717b2f8d1a9d026b205021`. Bootstrap and runner pins match the current files. Argument ordering and stdin flow in the `cat | env -i python -I -S -c` command select verification-only mode.

The bootstrap reads runner bytes through the no-follow descriptor path and compiles the captured immutable bytes after hashing. The runner rejects direct/unmarked entry and separately seals/rechecks the collector bytes before Bash reads them. The runner documentation now points to the Session 42 bootstrap chain, and trust assumptions are stated. Core found no remaining blocker. It did not run tests or the collector and made no changes.
