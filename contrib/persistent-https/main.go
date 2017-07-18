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

// The git-remote-persistent-https binary speeds up SSL operations by running
// a daemon job that keeps a connection open to a Git server. This ensures the
// git-remote-persistent-https--proxy is running and delegating execution
// to the git-remote-http binary with the http_proxy set to the daemon job.
// A unix socket is used to authenticate the proxy and discover the
// HTTP address. Note, both the client and proxy are included in the same
// binary.
package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"strings"
	"time"
)

var (
	forceProxy = flag.Bool("proxy", false, "Whether to start the binary in proxy mode")
	proxyBin   = flag.String("proxy_bin", "git-remote-persistent-https--proxy", "Path to the proxy binary")
	printLabel = flag.Bool("print_label", false, "Prints the build label for the binary")

	// Variable that should be defined through the -X linker flag.
	_BUILD_EMBED_LABEL string
)

const (
	defaultMaxIdleDuration    = 24 * time.Hour
	defaultPollUpdateInterval = 15 * time.Minute
)

func main() {
	flag.Parse()
	if *printLabel {
		// Short circuit execution to print the build label
		fmt.Println(buildLabel())
		return
	}

	var err error
	if *forceProxy || strings.HasSuffix(os.Args[0], "--proxy") {
		log.SetPrefix("git-remote-persistent-https--proxy: ")
		proxy := &Proxy{
			BuildLabel:         buildLabel(),
			MaxIdleDuration:    defaultMaxIdleDuration,
			PollUpdateInterval: defaultPollUpdateInterval,
		}
		err = proxy.Run()
	} else {
		log.SetPrefix("git-remote-persistent-https: ")
		client := &Client{
			ProxyBin: *proxyBin,
			Args:     flag.Args(),
		}
		err = client.Run()
	}
	if err != nil {
		log.Fatalln(err)
	}
}

func buildLabel() string {
	if _BUILD_EMBED_LABEL == "" {
		log.Println(`unlabeled build; build with "make" to label`)
	}
	return _BUILD_EMBED_LABEL
}
