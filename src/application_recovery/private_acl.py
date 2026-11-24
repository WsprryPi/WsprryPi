# SPDX-License-Identifier: MIT
"""Native extended grants supplement, and never replace, private mode checks."""
import ctypes
import errno
from functools import lru_cache
import sys


@lru_cache(maxsize=1)
def darwin_api():
    library = ctypes.CDLL(None, use_errno=True)
    signatures = {
        'acl_get_fd_np': (ctypes.c_void_p, (ctypes.c_int, ctypes.c_int)),
        'acl_valid': (ctypes.c_int, (ctypes.c_void_p,)),
        'acl_get_entry': (ctypes.c_int, (ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p))),
        'acl_get_tag_type': (ctypes.c_int, (ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint))),
        'acl_get_qualifier': (ctypes.c_void_p, (ctypes.c_void_p,)),
        'acl_free': (ctypes.c_int, (ctypes.c_void_p,)),
        'mbr_uid_to_uuid': (ctypes.c_int, (ctypes.c_uint, ctypes.c_void_p)),
    }
    for name, (result, arguments) in signatures.items():
        function = getattr(library, name)
        function.restype, function.argtypes = result, arguments
    return library


def require_private_acl(descriptor, owner):
    if sys.platform != 'darwin':
        return
    from .archive import require
    library = darwin_api()
    acl = library.acl_get_fd_np(descriptor, 0x100)  # ACL_TYPE_EXTENDED
    if not acl:
        require(ctypes.get_errno() in (errno.ENOENT, errno.ENOATTR))
        return
    try:
        require(library.acl_valid(acl) == 0)
        own = (ctypes.c_ubyte * 16)()
        require(library.mbr_uid_to_uuid(owner, own) == 0)
        entry = ctypes.c_void_p()
        following = library.acl_get_entry(acl, 0, ctypes.byref(entry))  # ACL_FIRST_ENTRY
        while following == 0:
            tag = ctypes.c_uint()
            require(library.acl_get_tag_type(entry, ctypes.byref(tag)) == 0)
            if tag.value == 1:  # ACL_EXTENDED_ALLOW
                principal = library.acl_get_qualifier(entry)
                require(bool(principal))
                try:
                    require(ctypes.string_at(principal, 16) == bytes(own))
                finally:
                    library.acl_free(principal)
            following = library.acl_get_entry(acl, -1, ctypes.byref(entry))  # ACL_NEXT_ENTRY
        require(following == -1 and ctypes.get_errno() == errno.EINVAL)
    finally:
        library.acl_free(acl)
