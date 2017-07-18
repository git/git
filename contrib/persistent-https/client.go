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
	"bufio"
	"errors"
	"fmt"
	"net"
	"net/url"
	"os"
	"os/exec"
	"strings"
	"syscall"
	"time"
)

type Client struct {
	ProxyBin string
	Args     []string

	insecure bool
}

func (c *Client) Run() error {
	if err := c.resolveArgs(); err != nil {
		return fmt.Errorf("resolveArgs() got error: %v", err)
	}

	// Connect to the proxy.
	uconn, hconn, addr, err := c.connect()
	if err != nil {
		return fmt.Errorf("connect() got error: %v", err)
	}
	// Keep the unix socket connection open for the duration of the request.
	defer uconn.Close()
	// Keep a connection to the HTTP server open, so no other user can
	// bind on the same address so long as the process is running.
	defer hconn.Close()

	// Start the git-remote-http subprocess.
	cargs := []string{"-c", fmt.Sprintf("http.proxy=%v", addr), "remote-http"}
	cargs = append(cargs, c.Args...)
	cmd := exec.Command("git", cargs...)

	for _, v := range os.Environ() {
		if !strings.HasPrefix(v, "GIT_PERSISTENT_HTTPS_SECURE=") {
			cmd.Env = append(cmd.Env, v)
		}
	}
	// Set the GIT_PERSISTENT_HTTPS_SECURE environment variable when
	// the proxy is using a SSL connection.  This allows credential helpers
	// to identify secure proxy connections, despite being passed an HTTP
	// scheme.
	if !c.insecure {
		cmd.Env = append(cmd.Env, "GIT_PERSISTENT_HTTPS_SECURE=1")
	}

	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		if eerr, ok := err.(*exec.ExitError); ok {
			if stat, ok := eerr.ProcessState.Sys().(syscall.WaitStatus); ok && stat.ExitStatus() != 0 {
				os.Exit(stat.ExitStatus())
			}
		}
		return fmt.Errorf("git-remote-http subprocess got error: %v", err)
	}
	return nil
}

func (c *Client) connect() (uconn net.Conn, hconn net.Conn, addr string, err error) {
	uconn, err = DefaultSocket.Dial()
	if err != nil {
		if e, ok := err.(*net.OpError); ok && (os.IsNotExist(e.Err) || e.Err == syscall.ECONNREFUSED) {
			if err = c.startProxy(); err == nil {
				uconn, err = DefaultSocket.Dial()
			}
		}
		if err != nil {
			return
		}
	}

	if addr, err = c.readAddr(uconn); err != nil {
		return
	}

	// Open a tcp connection to the proxy.
	if hconn, err = net.Dial("tcp", addr); err != nil {
		return
	}

	// Verify the address hasn't changed ownership.
	var addr2 string
	if addr2, err = c.readAddr(uconn); err != nil {
		return
	} else if addr != addr2 {
		err = fmt.Errorf("address changed after connect. got %q, want %q", addr2, addr)
		return
	}
	return
}

func (c *Client) readAddr(conn net.Conn) (string, error) {
	conn.SetDeadline(time.Now().Add(5 * time.Second))
	data := make([]byte, 100)
	n, err := conn.Read(data)
	if err != nil {
		return "", fmt.Errorf("error reading unix socket: %v", err)
	} else if n == 0 {
		return "", errors.New("empty data response")
	}
	conn.Write([]byte{1}) // Ack

	var addr string
	if addrs := strings.Split(string(data[:n]), "\n"); len(addrs) != 2 {
		return "", fmt.Errorf("got %q, wanted 2 addresses", data[:n])
	} else if c.insecure {
		addr = addrs[1]
	} else {
		addr = addrs[0]
	}
	return addr, nil
}

func (c *Client) startProxy() error {
	cmd := exec.Command(c.ProxyBin)
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return err
	}
	defer stdout.Close()
	if err := cmd.Start(); err != nil {
		return err
	}
	result := make(chan error)
	go func() {
		bytes, _, err := bufio.NewReader(stdout).ReadLine()
		if line := string(bytes); err == nil && line != "OK" {
			err = fmt.Errorf("proxy returned %q, want \"OK\"", line)
		}
		result <- err
	}()
	select {
	case err := <-result:
		return err
	case <-time.After(5 * time.Second):
		return errors.New("timeout waiting for proxy to start")
	}
	panic("not reachable")
}

func (c *Client) resolveArgs() error {
	if nargs := len(c.Args); nargs == 0 {
		return errors.New("remote needed")
	} else if nargs > 2 {
		return fmt.Errorf("want at most 2 args, got %v", c.Args)
	}

	// Rewrite the url scheme to be http.
	idx := len(c.Args) - 1
	rawurl := c.Args[idx]
	rurl, err := url.Parse(rawurl)
	if err != nil {
		return fmt.Errorf("invalid remote: %v", err)
	}
	c.insecure = rurl.Scheme == "persistent-http"
	rurl.Scheme = "http"
	c.Args[idx] = rurl.String()
	if idx != 0 && c.Args[0] == rawurl {
		c.Args[0] = c.Args[idx]
	}
	return nil
}
