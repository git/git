# Copyright 2012 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

BUILD_LABEL=$(shell cut -d" " -f3 ../../GIT-VERSION-FILE)
TAR_OUT=$(shell go env GOOS)_$(shell go env GOARCH).tar.gz

all: but-remote-persistent-https but-remote-persistent-https--proxy \
	but-remote-persistent-http

but-remote-persistent-https--proxy: but-remote-persistent-https
	ln -f -s but-remote-persistent-https but-remote-persistent-https--proxy

but-remote-persistent-http: but-remote-persistent-https
	ln -f -s but-remote-persistent-https but-remote-persistent-http

but-remote-persistent-https:
	case $$(go version) in \
	"go version go"1.[0-5].*) EQ=" " ;; *) EQ="=" ;; esac && \
	go build -o but-remote-persistent-https \
		-ldflags "-X main._BUILD_EMBED_LABEL$${EQ}$(BUILD_LABEL)"

clean:
	rm -f but-remote-persistent-http* *.tar.gz

tar: clean all
	@chmod 555 but-remote-persistent-https
	@tar -czf $(TAR_OUT) but-remote-persistent-http* README LICENSE
	@echo
	@echo "Created $(TAR_OUT)"
