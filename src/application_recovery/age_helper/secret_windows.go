// SPDX-License-Identifier: MIT
package main

import (
	"errors"
	"os"
	"strconv"
	"syscall"
)

// The trusted native launcher inherits exactly this private pipe handle. Its
// numeric identifier is not a passphrase; secret bytes never enter arguments,
// environment variables, or files. A CRT descriptor is not a Win32 handle.
func secretChannel(args []string) (*os.File, error) {
	if len(args) != 3 || args[1] != "--secret-pipe-handle" {
		return nil, errors.New("unsupported invocation")
	}
	value, err := strconv.ParseUint(args[2], 10, strconv.IntSize)
	if err != nil || value == 0 || value == uint64(^uintptr(0)) || strconv.FormatUint(value, 10) != args[2] {
		return nil, errors.New("missing secret pipe")
	}
	kind, err := syscall.GetFileType(syscall.Handle(value))
	if err != nil || kind != syscall.FILE_TYPE_PIPE {
		return nil, errors.New("secret handle is not a pipe")
	}
	descriptor := os.NewFile(uintptr(value), "passphrase-pipe")
	if descriptor == nil {
		return nil, errors.New("missing secret pipe")
	}
	return descriptor, nil
}
