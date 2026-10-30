"""Descriptor-relative, bounded filesystem primitives for synthetic candidates."""
from __future__ import annotations

from dataclasses import dataclass
import errno
import hashlib
import os
import stat
from .budget import Budget
from .contracts import BudgetExhausted, ResultStatus, UnsafePath

DIR_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW
FILE_FLAGS = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
WRITE_FLAGS = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW


def _basename(name: str) -> str:
    if not isinstance(name, str) or name in ("", ".", "..") or "/" in name or "\x00" in name:
        raise UnsafePath("only a single safe path component is accepted")
    return name


def open_absolute_directory(path: str) -> "SafeDirectory":
    """Open every absolute path component without following any symlink."""
    if not isinstance(path, str) or not path.startswith("/"):
        raise UnsafePath("directory path must be absolute")
    parts = path.split("/")[1:]
    if not parts or any(part in ("", ".", "..") for part in parts):
        raise UnsafePath("directory path must be canonical and traversal-free")
    fd = os.open("/", DIR_FLAGS)
    try:
        for part in parts:
            next_fd = os.open(part, DIR_FLAGS, dir_fd=fd)
            previous_fd = fd
            fd = next_fd
            os.close(previous_fd)
        info = os.fstat(fd)
        if not stat.S_ISDIR(info.st_mode):
            raise UnsafePath("opened object is not a directory")
        return SafeDirectory(fd)
    except BaseException:
        os.close(fd)
        raise


@dataclass
class ReadResult:
    data: bytes
    sha256: str
    bytes_read: int
    status: ResultStatus


@dataclass
class WriteResult:
    name: str
    sha256: str
    bytes_written: int


class SafeFile:
    def __init__(self, fd: int, before: os.stat_result, source: str, budget: Budget, cap: int):
        self.__fd = fd
        self.before = before
        self.source = source
        self.budget = budget
        self.cap = cap
        self._closed = False

    def _check_stable(self, count: int) -> None:
        self.budget.access_attempt("file-metadata")
        try:
            after = os.fstat(self.__fd)
        except PermissionError:
            self.budget.access_denied_event("file-metadata")
            raise
        except OSError:
            self.budget.access_error("file-metadata")
            raise
        self.budget.access_success("file-metadata")
        before_id = (self.before.st_dev, self.before.st_ino, self.before.st_mode,
                     self.before.st_nlink, self.before.st_size,
                     self.before.st_mtime_ns, self.before.st_ctime_ns)
        after_id = (after.st_dev, after.st_ino, after.st_mode, after.st_nlink,
                    after.st_size, after.st_mtime_ns, after.st_ctime_ns)
        if before_id != after_id or count != self.before.st_size:
            raise UnsafePath("file identity or metadata changed while being read")

    def read_all(self) -> ReadResult:
        if self.before.st_size > self.cap:
            raise BudgetExhausted("file exceeds bounded-read limit")
        chunks: list[bytes] = []
        total = 0
        digest = hashlib.sha256()
        while total < self.before.st_size:
            self.budget.check_deadline()
            wanted = min(64 * 1024, self.before.st_size - total)
            amount = self.budget.read_allowance(self.source, wanted)
            self.budget.access_attempt("file-read")
            try:
                block = os.read(self.__fd, amount)
            except InterruptedError:
                self.budget.operation_interrupted("file-read")
                continue
            except PermissionError:
                self.budget.access_denied_event("file-read")
                raise
            except OSError:
                self.budget.access_error("file-read")
                raise
            if not block:
                self.budget.access_error("file-read")
                raise UnsafePath("unexpected EOF in regular file")
            self.budget.access_success("file-read")
            self.budget.consume_read(len(block), self.source)
            self.budget.check_deadline()
            total += len(block)
            chunks.append(block)
            digest.update(block)
        try:
            self._check_stable(total)
        except OSError:
            self.budget.access_error("file-read")
            raise
        self.budget.record_file_read()
        return ReadResult(b"".join(chunks), digest.hexdigest(), total, ResultStatus.COMPLETE)

    def hash_all(self) -> ReadResult:
        if self.before.st_size > self.cap:
            raise BudgetExhausted("file exceeds bounded-hash limit")
        digest = hashlib.sha256()
        total = 0
        while total < self.before.st_size:
            self.budget.check_deadline()
            wanted = min(1024 * 1024, self.before.st_size - total)
            amount = self.budget.read_allowance(self.source, wanted)
            self.budget.access_attempt("file-read")
            try:
                block = os.read(self.__fd, amount)
            except InterruptedError:
                self.budget.operation_interrupted("file-read")
                continue
            except PermissionError:
                self.budget.access_denied_event("file-read")
                raise
            except OSError:
                self.budget.access_error("file-read")
                raise
            if not block:
                self.budget.access_error("file-read")
                raise UnsafePath("unexpected EOF while hashing regular file")
            self.budget.access_success("file-read")
            self.budget.consume_read(len(block), self.source)
            self.budget.check_deadline()
            total += len(block)
            digest.update(block)
        try:
            self._check_stable(total)
        except OSError:
            self.budget.access_error("file-read")
            raise
        self.budget.record_file_read()
        return ReadResult(b"", digest.hexdigest(), total, ResultStatus.COMPLETE)

    def close(self) -> None:
        if not self._closed:
            os.close(self.__fd)
            self._closed = True

    def __enter__(self) -> "SafeFile":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


class SafeDirectory:
    def __init__(self, fd: int):
        self.__fd = fd
        self._closed = False
        info = os.fstat(fd)
        if not stat.S_ISDIR(info.st_mode):
            raise UnsafePath("directory descriptor is not a directory")

    def child_directory(self, name: str) -> "SafeDirectory":
        fd = os.open(_basename(name), DIR_FLAGS, dir_fd=self.__fd)
        try:
            return SafeDirectory(fd)
        except BaseException:
            # SafeDirectory owns the descriptor after successful construction.
            raise

    def open_regular(self, name: str, *, source: str, budget: Budget,
                     max_bytes: int | None = None) -> SafeFile:
        leaf = _basename(name)
        budget.begin_input_open()
        try:
            fd = os.open(leaf, FILE_FLAGS, dir_fd=self.__fd)
        except PermissionError:
            budget.access_denied_event("file-open")
            raise
        except InterruptedError:
            budget.operation_interrupted("file-open")
            raise
        except OSError:
            budget.access_error("file-open")
            raise
        try:
            budget.access_attempt("file-metadata")
            try:
                before = os.fstat(fd)
            except PermissionError:
                budget.access_denied_event("file-metadata")
                raise
            except OSError:
                budget.access_error("file-metadata")
                raise
            budget.access_success("file-metadata")
            budget.input_opened(source, declared_size=before.st_size,
                                per_file_limit=budget.limits.bytes_per_file if max_bytes is None else max_bytes)
            if not stat.S_ISREG(before.st_mode):
                raise UnsafePath("opened object is not a regular file")
            if before.st_nlink != 1:
                raise UnsafePath("hard-linked input is rejected")
            cap = budget.limits.bytes_per_file if max_bytes is None else max_bytes
            if isinstance(cap, bool) or not isinstance(cap, int) or cap < 0 or cap > budget.limits.bytes_per_file:
                raise BudgetExhausted("per-file limit exceeds fixed hard maximum")
            if before.st_size > cap:
                raise BudgetExhausted("file exceeds bounded-read limit")
            return SafeFile(fd, before, source, budget, cap)
        except BaseException:
            os.close(fd)
            raise

    def create_child_exclusive(self, name: str) -> "SafeDirectory":
        leaf = _basename(name)
        os.mkdir(leaf, mode=0o700, dir_fd=self.__fd)
        fd = os.open(leaf, DIR_FLAGS, dir_fd=self.__fd)
        try:
            info = os.fstat(fd)
            entry = os.stat(leaf, dir_fd=self.__fd, follow_symlinks=False)
            if (entry.st_dev, entry.st_ino) != (info.st_dev, info.st_ino):
                raise UnsafePath("output directory name no longer identifies the opened directory")
            if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
                raise UnsafePath("new output directory has unsafe type, owner, or mode")
            return SafeDirectory(fd)
        except BaseException:
            os.close(fd)
            raise

    def write_new_file(self, name: str, data: bytes, *, budget: Budget) -> WriteResult:
        return self._write_file(name, data, budget=budget, finalization_kind=None)

    def write_finalization_file(self, name: str, data: bytes, *, budget: Budget,
                                file_kind: str) -> WriteResult:
        return self._write_file(name, data, budget=budget, finalization_kind=file_kind)

    def _write_file(self, name: str, data: bytes, *, budget: Budget,
                    finalization_kind: str | None) -> WriteResult:
        leaf = _basename(name)
        if not isinstance(data, bytes):
            raise TypeError("output must be bytes")
        if finalization_kind is None:
            file_cap = budget.limits.output_file_bytes
        elif finalization_kind == "manifest":
            file_cap = budget.limits.manifest_bytes
        elif finalization_kind == "seal":
            file_cap = budget.limits.seal_bytes
        else:
            raise ValueError("unknown finalization file kind")
        if len(data) > file_cap:
            budget.mark_limit(f"{finalization_kind or 'output'}_file_bytes")
            raise BudgetExhausted("output exceeds its per-file persistence budget")
        budget.output_open_attempted()
        try:
            fd = os.open(leaf, WRITE_FLAGS, 0o600, dir_fd=self.__fd)
        except PermissionError:
            budget.access_denied_event("file-open")
            raise
        except InterruptedError:
            budget.operation_interrupted("file-open")
            raise
        except OSError:
            budget.access_error("file-open")
            raise
        budget.output_opened()
        written_total = 0
        digest = hashlib.sha256()
        try:
            view = memoryview(data)
            while view:
                budget.check_deadline()
                persist_ceiling = budget.limits.bytes_persisted
                if finalization_kind is None:
                    persist_ceiling -= budget.finalization_reserve
                allowed = min(
                    len(view),
                    file_cap - written_total,
                    persist_ceiling - budget.bytes_persisted,
                )
                if allowed <= 0:
                    limit_name = "bytes_persisted" if finalization_kind is not None else "adapter_output_reserve"
                    budget.mark_limit(limit_name)
                    raise BudgetExhausted("persisted-byte budget or protected reserve reached")
                part = view[:allowed]
                budget.access_attempt("file-write")
                try:
                    count = os.write(fd, part)
                except InterruptedError:
                    budget.operation_interrupted("file-write")
                    budget.access_attempt("file-write")
                    continue
                except KeyboardInterrupt:
                    budget.operation_interrupted("file-write")
                    raise
                except OSError:
                    budget.access_error("file-write")
                    raise
                if count <= 0:
                    budget.access_error("file-write")
                    raise OSError(errno.EIO, "write made no progress")
                budget.access_success("file-write")
                if finalization_kind is None:
                    budget.record_persisted(count)
                else:
                    budget.record_finalization_persisted(count, file_kind=finalization_kind)
                if written_total + count > file_cap:
                    raise BudgetExhausted("single output exceeds fixed output-file budget")
                written_total += count
                digest.update(part[:count])
                view = view[count:]
                budget.check_deadline()
            budget.access_attempt("file-fsync")
            try:
                os.fsync(fd)
            except KeyboardInterrupt:
                budget.operation_interrupted("file-fsync")
                raise
            except OSError:
                budget.access_error("file-fsync")
                raise
            budget.access_success("file-fsync")
            budget.check_deadline()
        except BaseException as exc:
            # Keep the exclusive partial file as evidence; the caller must mark
            # the acquisition incomplete and may not retry or overwrite it.
            try:
                budget.access_attempt("file-fsync")
                os.fsync(fd)
                budget.access_success("file-fsync")
            except OSError:
                budget.access_error("file-fsync")
                pass
            raise PartialWriteError(leaf, written_total, exc, digest.hexdigest()) from exc
        finally:
            os.close(fd)
        try:
            budget.access_attempt("directory-fsync")
            try:
                os.fsync(self.__fd)
            except KeyboardInterrupt:
                budget.operation_interrupted("directory-fsync")
                raise
            except OSError:
                budget.access_error("directory-fsync")
                raise
            budget.access_success("directory-fsync")
            budget.check_deadline()
        except (OSError, BudgetExhausted) as exc:
            raise PartialWriteError(leaf, written_total, exc, digest.hexdigest()) from exc
        return WriteResult(leaf, digest.hexdigest(), written_total)

    def close(self) -> None:
        if not self._closed:
            os.close(self.__fd)
            self._closed = True

    def __enter__(self) -> "SafeDirectory":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


class PartialWriteError(OSError):
    def __init__(self, name: str, bytes_written: int, cause: BaseException, sha256: str):
        super().__init__(f"partial output {name}: {bytes_written} bytes written; {cause}")
        self.name = name
        self.bytes_written = bytes_written
        self.cause = cause
        self.sha256 = sha256
