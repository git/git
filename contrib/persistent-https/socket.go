// Copyright 2012 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package main

import (
	"fmt"
	"log"
	"net"
	"os"
	"path/filepath"
	"syscall"
)

// A Socket is a wrapper around a Unix socket that verifies directory
// permissions.
type Socket struct {
	Dir string
}

func defaultDir() string {
	sockPath := ".git-credential-cache"
	if home := os.Getenv("HOME"); home != "" {
		return filepath.Join(home, sockPath)
	}
	log.Printf("socket: cannot find HOME path. using relative directory %q for socket", sockPath)
	return sockPath
}

// DefaultSocket is a Socket in the $HOME/.git-credential-cache directory.
var DefaultSocket = Socket{Dir: defaultDir()}

// Listen announces the local network address of the unix socket. The
// permissions on the socket directory are verified before attempting
// the actual listen.
func (s Socket) Listen() (net.Listener, error) {
	network, addr := "unix", s.Path()
	if err := s.mkdir(); err != nil {
		return nil, &net.OpError{Op: "listen", Net: network, Addr: &net.UnixAddr{Name: addr, Net: network}, Err: err}
	}
	return net.Listen(network, addr)
}

// Dial connects to the unix socket. The permissions on the socket directory
// are verified before attempting the actual dial.
func (s Socket) Dial() (net.Conn, error) {
	network, addr := "unix", s.Path()
	if err := s.checkPermissions(); err != nil {
		return nil, &net.OpError{Op: "dial", Net: network, Addr: &net.UnixAddr{Name: addr, Net: network}, Err: err}
	}
	return net.Dial(network, addr)
}

// Path returns the fully specified file name of the unix socket.
func (s Socket) Path() string {
	return filepath.Join(s.Dir, "persistent-https-proxy-socket")
}

func (s Socket) mkdir() error {
	if err := s.checkPermissions(); err == nil {
		return nil
	} else if !os.IsNotExist(err) {
		return err
	}
	if err := os.MkdirAll(s.Dir, 0700); err != nil {
		return err
	}
	return s.checkPermissions()
}

func (s Socket) checkPermissions() error {
	fi, err := os.Stat(s.Dir)
	if err != nil {
		return err
	}
	if !fi.IsDir() {
		return fmt.Errorf("socket: got file, want directory for %q", s.Dir)
	}
	if fi.Mode().Perm() != 0700 {
		return fmt.Errorf("socket: got perm %o, want 700 for %q", fi.Mode().Perm(), s.Dir)
	}
	if st := fi.Sys().(*syscall.Stat_t); int(st.Uid) != os.Getuid() {
		return fmt.Errorf("socket: got uid %d, want %d for %q", st.Uid, os.Getuid(), s.Dir)
	}
	return nil
}
