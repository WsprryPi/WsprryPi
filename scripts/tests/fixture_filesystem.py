"""Owned temporary roots and no-follow observations for offline fixtures."""
import hashlib
import os
from pathlib import Path
import stat
import subprocess
import tempfile


def temporary_root(case, prefix):
    temporary = tempfile.TemporaryDirectory(prefix=prefix)
    case.addCleanup(temporary.cleanup)
    return Path(temporary.name).resolve()


def bash_quote(value):
    return subprocess.run(["bash", "-c", 'printf "%q" "$1"', "bash-quote", str(value)],
                          text=True, stdout=subprocess.PIPE, check=True, timeout=10).stdout


def snapshot(root):
    result = {}

    def visit(directory, name, relative):
        info = os.stat(name, dir_fd=directory, follow_symlinks=False)
        identity = (info.st_dev, info.st_ino)
        content = ""
        if stat.S_ISREG(info.st_mode) or stat.S_ISDIR(info.st_mode):
            flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK
            if stat.S_ISDIR(info.st_mode): flags |= os.O_DIRECTORY
            fd = os.open(name, flags, dir_fd=directory)
            try:
                opened = os.fstat(fd)
                if (opened.st_dev, opened.st_ino) != identity:
                    raise AssertionError("fixture changed during observation")
                if stat.S_ISREG(info.st_mode):
                    with os.fdopen(os.dup(fd), "rb") as source:
                        content = hashlib.file_digest(source, "sha256").hexdigest()
                else:
                    for child in sorted(os.listdir(fd)):
                        visit(fd, child, child if relative == "." else relative + "/" + child)
            finally:
                os.close(fd)
        elif stat.S_ISLNK(info.st_mode):
            content = os.readlink(name, dir_fd=directory)
        result[relative] = (stat.S_IFMT(info.st_mode), stat.S_IMODE(info.st_mode),
            info.st_uid, info.st_gid, info.st_nlink, info.st_mtime_ns, identity, content)

    root = root.absolute()
    if ".." in root.parts:
        raise ValueError("fixture observation cannot traverse parent components")
    parent = os.open(root.anchor, os.O_RDONLY | os.O_DIRECTORY)
    try:
        for part in root.parts[1:-1]:
            descendant = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
            os.close(parent)
            parent = descendant
        visit(parent, root.name, ".")
    finally:
        os.close(parent)
    return result
