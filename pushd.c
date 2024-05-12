/*
 * Copyright (c) 2019 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define _GNU_SOURCE

#define CONFIG_SIZE (500)
#define TITLE_SIZE (250)

#define TOKEN_SET (1 << 1)
#define USER_SET (1 << 2)
#define PROXY_SET (1 << 3)
#define TITLE_SET (1 << 4)
#define DNSCACHE_SET (1 << 5)
#define CONFIG_ERROR (1 << 6)

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <syslog.h>

#include <curl/curl.h>

/* #define PUSHD_TOKEN "YOUR APITOKEN" */
/* #define PUSHD_USER "YOUR USERTOKEN" */
/* #define PUSHD_PROXY "http://your.proxy.example.org:8181" */
/* #define PUSHD_TITLE "optional title (default: hostname)" */
/* #define PUSHD_DNSCACHE false (default: false)*/

#define PUSHOVER_API_PREFIX "https://"
#define PUSHOVER_HOSTNAME "api.pushover.net"
#define PUSHOVER_API_SUFFIX "/1/messages.json"

typedef struct config
{
	unsigned set;
	char token[CONFIG_SIZE];
	char user[CONFIG_SIZE];
	char proxy[CONFIG_SIZE];
	char title[TITLE_SIZE];
	bool dns_cache;
} CONFIG;

int read_config(char *, CONFIG *);
int parse_config(char *, CONFIG *);
void push(char *, CONFIG *);
void replace_newline(char *);
void validate_pushover_hostname();
void resolve_pushover_ip();

char pushover_api_url[100];

int read_config(char *file_name, CONFIG *config)
{
	FILE *file;
	file = fopen(file_name, "r");

	syslog(LOG_INFO, "Reading config from: %s", file_name);

	char buffer[BUFSIZ];
	int result = 0;
	while (fgets(buffer, sizeof(buffer), file) != NULL)
	{
		if (parse_config(buffer, config) == CONFIG_ERROR)
		{
			result = CONFIG_ERROR;
			break;
		}
	}

	fclose(file);

	char hostname[TITLE_SIZE / 2];
	if (gethostname(hostname, sizeof(hostname)))
	{
		syslog(LOG_ERR, "Abort: Error reading hostname");
		err(1, "gethostname");
	}

	// if no title, take hostname as title
	if (!(config->set & TITLE_SET))
	{
		sprintf(config->title, "%s", hostname);
	}

	// if no dnscache, set it to 0
	if (!(config->set & DNSCACHE_SET))
	{
		config->dns_cache = false;
	}

	return result;
}

// Parse the buffer for config info. Return an error code or 0 for no error.
int parse_config(char *buf, CONFIG *config)
{
	char dummy[CONFIG_SIZE];
	if (sscanf(buf, " %s", dummy) == EOF)
		return 0; // blank line
	if (sscanf(buf, " %[#]", dummy) == 1)
		return 0; // comment

	// trim whitespace, and get up to the equal sign
	char *config_name = strtok(buf, "= \t\n\r\f\v");
	if (config_name && strncmp(config_name, "PUSHD_", 6) == 0)
	{
		char *config_value = strtok(NULL, "= #\t\n\r\f\v");
		while (config_value != NULL)
		{
			if (strcmp(config_name, "PUSHD_TOKEN") == 0)
			{
				if (config->set & TOKEN_SET)
					return TOKEN_SET;
				config->set |= TOKEN_SET;
				strcpy(config->token, config_value);
				return 0;
			}
			else if (strcmp(config_name, "PUSHD_USER") == 0)
			{
				if (config->set & USER_SET)
					return USER_SET;
				config->set |= USER_SET;
				strcpy(config->user, config_value);
				return 0;
			}
			else if (strcmp(config_name, "PUSHD_PROXY") == 0)
			{
				if (config->set & PROXY_SET)
					return PROXY_SET;
				config->set |= PROXY_SET;
				strcpy(config->proxy, config_value);
				return 0;
			}
			else if (strcmp(config_name, "PUSHD_TITLE") == 0)
			{
				if (config->set & TITLE_SET)
					return TITLE_SET;
				config->set |= TITLE_SET;
				strcpy(config->title, config_value);
				return 0;
			}
			else if (strcmp(config_name, "PUSHD_DNSCACHE") == 0)
			{
				if (config->set & DNSCACHE_SET)
					return DNSCACHE_SET;
				config->set |= DNSCACHE_SET;
				if (strcmp(config_value, "0") != 0 && strcasecmp(config_value, "false") != 0)
					config->dns_cache = true;
				else
					config->dns_cache = false;
				return 0;
			}
			config_value = strtok(NULL, " =");
		}
	}
	return CONFIG_ERROR;
}

void validate_pushover_hostname()
{
	CURL *curl;
	curl = curl_easy_init();
	if (curl == NULL)
	{
		err(1, "curl_easy_init: dns_cache");
	}
	curl_easy_setopt(curl, CURLOPT_URL, PUSHOVER_API_PREFIX PUSHOVER_HOSTNAME PUSHOVER_API_SUFFIX);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	if (curl_easy_perform(curl) != CURLE_OK)
	{
		syslog(LOG_ERR, "Abort: Error validating '" PUSHOVER_API_PREFIX PUSHOVER_HOSTNAME PUSHOVER_API_SUFFIX);
		err(1, "resolve_pushover_ip: check failed");
	}
	curl_easy_cleanup(curl);
}

void resolve_pushover_ip()
{
	validate_pushover_hostname();

	struct addrinfo hints, *res;
	int error;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if ((error = getaddrinfo(PUSHOVER_HOSTNAME, NULL, &hints, &res)))
	{
		syslog(LOG_ERR, "Abort: Error getting '" PUSHOVER_HOSTNAME "' address info");
		err(1, "getaddrinfo: %s", gai_strerror(error));
	}
	char *pushover_ip = inet_ntoa(((struct sockaddr_in *)res->ai_addr)->sin_addr);
	sprintf(pushover_api_url, "%s%s%s", PUSHOVER_API_PREFIX, pushover_ip, PUSHOVER_API_SUFFIX);
	syslog(LOG_DEBUG, "Pushover hostname cached to: '%s'", pushover_ip);

	freeaddrinfo(res);
}

void replace_newline(char *str)
{
	const char *newline = "\\n";
	const char *replacement = "\n";
	size_t newline_len = strlen(newline);
	size_t replacement_len = strlen(replacement);

	char *pos = strstr(str, newline);
	while (pos != NULL)
	{
		memmove(pos + replacement_len, pos + newline_len, strlen(pos + newline_len) + 1);
		memcpy(pos, replacement, replacement_len);
		pos = strstr(pos + replacement_len, newline);
	}
}

void push(char *msg, CONFIG *config)
{
	CURL *curl;
	char *opts = NULL;
	char *output;

	if (msg == NULL || strlen(msg) == 0)
		return;

	curl = curl_easy_init();
	if (curl == NULL)
		return;

	replace_newline(msg);
	output = curl_easy_escape(curl, msg, 0);
	if (output == NULL)
		goto out;

	asprintf(&opts, "token=%s&user=%s&title=%s&message=%s", config->token, config->user, config->title, output);
	curl_free(output);

	if (opts == NULL)
		goto out;

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Host: " PUSHOVER_HOSTNAME);
	curl_easy_setopt(curl, CURLOPT_URL, pushover_api_url);
	if (config->dns_cache == true)
	{
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
	}
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, opts);
	// curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	if (config->set & PROXY_SET)
	{
		curl_easy_setopt(curl, CURLOPT_PROXY, config->proxy);
	}

	curl_easy_perform(curl);
	curl_free(opts);
	curl_slist_free_all(headers);
out:
	curl_easy_cleanup(curl);
}

int main()
{
	openlog("pushd", LOG_PID | LOG_CONS, LOG_USER);

	// Parse config
	CONFIG *config = malloc(sizeof(CONFIG));
	config->set = 0u;

	char chunk[1024];
	char *line;
	size_t len;

	if (read_config("/etc/pushd.conf", config) == CONFIG_ERROR)
	{
		syslog(LOG_ERR, "Abort: Error parsing config");
		err(1, "config");
	}

	curl_global_init(CURL_GLOBAL_ALL);

	if (config->dns_cache == true)
		resolve_pushover_ip();
	else
		sprintf(pushover_api_url, "%s%s%s", PUSHOVER_API_PREFIX, PUSHOVER_HOSTNAME, PUSHOVER_API_SUFFIX);

	// Read lines and push them
	for (;;)
	{
		line = NULL;
		getline(&line, &len, stdin);
		if (line == NULL)
			break;
		strncpy(chunk, line, sizeof(chunk) - 1);
		chunk[sizeof(chunk) - 1] = '\0';
		push(chunk, config);
		free(line);
	}

	curl_global_cleanup();

	free(config);

	closelog();
	return 0;
}
