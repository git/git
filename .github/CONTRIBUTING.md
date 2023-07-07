openapi: '3.0.2'
info:
  title: API Title
  version: '1.0'
servers:
  - url: https://api.server.test/v1
paths:
  /test:
    get:
      responses:
        '200':
          description: OK
