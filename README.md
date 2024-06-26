# pushd

`pushd` takes messages on stdin and pipes each line to your pushover
account. It depends on cURL for major laziness.

Supports multi-line messages by converting literal `\n` to newline characters.

This is a Linux port of https://github.com/bluerise/pushd

You can use it with rsyslog by adding a file to _/etc/rsyslog.d_ containing:

```
# Pushover alerts template
template(name="pushdmsg" type="string" string="%msg%\n")

# Send emergency levels to pushover
*.=emerg action(type="omprog" binary="/usr/local/bin/pushd" template="pushdmsg")
```

Make sure you load the `omprog` rsyslog module, by adding the following to `/etc/rsyslog.conf`

```
module(load="omprog")
```

Then restart rsyslog:

```
sudo systemctl restart rsyslog.service
```

## Building

```
make
```

### Requisites

- `libcurl4-openssl-dev` or equivalent
- `gmake`
- `gcc`

## Installing

```
sudo make install
```

## Configuration

Installation copies an example default config to `/etc/pushd.conf`:

```
PUSHD_USER  = USER # Your Pushover user key
PUSHD_TOKEN = TOKEN
PUSHD_PROXY = PROXY # optional
PUSHD_TITLE = TITLE # optional, defaults to hostname
PUSHD_DNSCACHE = false # optional, caches the dns resolution for `api.pushover.net` at init
```

> [!IMPORTANT]
> When using the DNS resolution cache, `curl` won't validate the certificate's name hostname.
> See [CURLOPT_SSL_VERIFYHOST](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYHOST.html).
