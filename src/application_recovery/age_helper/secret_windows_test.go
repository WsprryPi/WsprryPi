// SPDX-License-Identifier: MIT
package main

import (
	"encoding/binary"
	"os"
	"strconv"
	"syscall"
	"testing"
)

func TestNativeSecretPipe(t *testing.T) {
	for _, args := range [][]string{nil, {"encrypt"}, {"encrypt", "--secret-pipe-handle", "0"},
		{"encrypt", "--secret-pipe-handle", "-1"}, {"encrypt", "--secret-pipe-handle", "0004"},
		{"encrypt", "--secret-pipe-handle", "18446744073709551616"}, {"encrypt", "--other", "4"}} {
		if file, err := secretChannel(args); err == nil || file != nil {
			t.Fatal("invalid native pipe invocation accepted")
		}
	}
	var reader, writer syscall.Handle
	if err := syscall.CreatePipe(&reader, &writer, nil, 4096); err != nil {
		t.Fatal(err)
	}
	output := os.NewFile(uintptr(writer), "secret-test-writer")
	if output == nil {
		syscall.CloseHandle(reader)
		syscall.CloseHandle(writer)
		t.Fatal("missing writer")
	}
	descriptor, err := secretChannel([]string{"encrypt", "--secret-pipe-handle", strconv.FormatUint(uint64(reader), 10)})
	if err != nil {
		syscall.CloseHandle(reader)
		output.Close()
		t.Fatal(err)
	}
	defer descriptor.Close()
	if err := binary.Write(output, binary.BigEndian, uint32(6)); err != nil {
		t.Fatal(err)
	}
	if _, err := output.Write([]byte("phrase")); err != nil {
		t.Fatal(err)
	}
	if err := output.Close(); err != nil {
		t.Fatal(err)
	}
	value, err := secret(descriptor)
	if err != nil || value != "phrase" {
		t.Fatal("private native frame refused")
	}
	temporary, err := os.CreateTemp(t.TempDir(), "not-pipe")
	if err != nil {
		t.Fatal(err)
	}
	defer temporary.Close()
	if file, err := secretChannel([]string{"encrypt", "--secret-pipe-handle", strconv.FormatUint(uint64(temporary.Fd()), 10)}); err == nil || file != nil {
		t.Fatal("regular file admitted as a secret pipe")
	}
}
