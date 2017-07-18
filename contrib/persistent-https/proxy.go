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
	"net/http"
	"net/http/httputil"
	"os"
	"os/exec"
	"os/signal"
	"sync"
	"syscall"
	"time"
)

type Proxy struct {
	BuildLabel         string
	MaxIdleDuration    time.Duration
	PollUpdateInterval time.Duration

	ul        net.Listener
	httpAddr  string
	httpsAddr string
}

func (p *Proxy) Run() error {
	hl, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return fmt.Errorf("http listen failed: %v", err)
	}
	defer hl.Close()

	hsl, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return fmt.Errorf("https listen failed: %v", err)
	}
	defer hsl.Close()

	p.ul, err = DefaultSocket.Listen()
	if err != nil {
		c, derr := DefaultSocket.Dial()
		if derr == nil {
			c.Close()
			fmt.Println("OK\nA proxy is already running... exiting")
			return nil
		} else if e, ok := derr.(*net.OpError); ok && e.Err == syscall.ECONNREFUSED {
			// Nothing is listening on the socket, unlink it and try again.
			syscall.Unlink(DefaultSocket.Path())
			p.ul, err = DefaultSocket.Listen()
		}
		if err != nil {
			return fmt.Errorf("unix listen failed on %v: %v", DefaultSocket.Path(), err)
		}
	}
	defer p.ul.Close()
	go p.closeOnSignal()
	go p.closeOnUpdate()

	p.httpAddr = hl.Addr().String()
	p.httpsAddr = hsl.Addr().String()
	fmt.Printf("OK\nListening on unix socket=%v http=%v https=%v\n",
		p.ul.Addr(), p.httpAddr, p.httpsAddr)

	result := make(chan error, 2)
	go p.serveUnix(result)
	go func() {
		result <- http.Serve(hl, &httputil.ReverseProxy{
			FlushInterval: 500 * time.Millisecond,
			Director:      func(r *http.Request) {},
		})
	}()
	go func() {
		result <- http.Serve(hsl, &httputil.ReverseProxy{
			FlushInterval: 500 * time.Millisecond,
			Director: func(r *http.Request) {
				r.URL.Scheme = "https"
			},
		})
	}()
	return <-result
}

type socketContext struct {
	sync.WaitGroup
	mutex sync.Mutex
	last  time.Time
}

func (sc *socketContext) Done() {
	sc.mutex.Lock()
	defer sc.mutex.Unlock()
	sc.last = time.Now()
	sc.WaitGroup.Done()
}

func (p *Proxy) serveUnix(result chan<- error) {
	sockCtx := &socketContext{}
	go p.closeOnIdle(sockCtx)

	var err error
	for {
		var uconn net.Conn
		uconn, err = p.ul.Accept()
		if err != nil {
			err = fmt.Errorf("accept failed: %v", err)
			break
		}
		sockCtx.Add(1)
		go p.handleUnixConn(sockCtx, uconn)
	}
	sockCtx.Wait()
	result <- err
}

func (p *Proxy) handleUnixConn(sockCtx *socketContext, uconn net.Conn) {
	defer sockCtx.Done()
	defer uconn.Close()
	data := []byte(fmt.Sprintf("%v\n%v", p.httpsAddr, p.httpAddr))
	uconn.SetDeadline(time.Now().Add(5 * time.Second))
	for i := 0; i < 2; i++ {
		if n, err := uconn.Write(data); err != nil {
			log.Printf("error sending http addresses: %+v\n", err)
			return
		} else if n != len(data) {
			log.Printf("sent %d data bytes, wanted %d\n", n, len(data))
			return
		}
		if _, err := uconn.Read([]byte{0, 0, 0, 0}); err != nil {
			log.Printf("error waiting for Ack: %+v\n", err)
			return
		}
	}
	// Wait without a deadline for the client to finish via EOF
	uconn.SetDeadline(time.Time{})
	uconn.Read([]byte{0, 0, 0, 0})
}

func (p *Proxy) closeOnIdle(sockCtx *socketContext) {
	for d := p.MaxIdleDuration; d > 0; {
		time.Sleep(d)
		sockCtx.Wait()
		sockCtx.mutex.Lock()
		if d = sockCtx.last.Add(p.MaxIdleDuration).Sub(time.Now()); d <= 0 {
			log.Println("graceful shutdown from idle timeout")
			p.ul.Close()
		}
		sockCtx.mutex.Unlock()
	}
}

func (p *Proxy) closeOnUpdate() {
	for {
		time.Sleep(p.PollUpdateInterval)
		if out, err := exec.Command(os.Args[0], "--print_label").Output(); err != nil {
			log.Printf("error polling for updated binary: %v\n", err)
		} else if s := string(out[:len(out)-1]); p.BuildLabel != s {
			log.Printf("graceful shutdown from updated binary: %q --> %q\n", p.BuildLabel, s)
			p.ul.Close()
			break
		}
	}
}

func (p *Proxy) closeOnSignal() {
	ch := make(chan os.Signal, 10)
	signal.Notify(ch, os.Interrupt, os.Kill, os.Signal(syscall.SIGTERM), os.Signal(syscall.SIGHUP))
	sig := <-ch
	p.ul.Close()
	switch sig {
	case os.Signal(syscall.SIGHUP):
		log.Printf("graceful shutdown from signal: %v\n", sig)
	default:
		log.Fatalf("exiting from signal: %v\n", sig)
	}
}
