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
#define CONFIG_ERROR (1 << 5)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>

#include <curl/curl.h>

/* #define PUSHD_TOKEN "YOUR APITOKEN" */
/* #define PUSHD_USER "YOUR USERTOKEN" */
/* #define PUSHD_PROXY "http://your.proxy.example.org:8181" */
/* #define PUSHD_TITLE "optional title (default: hostname)" */

typedef struct config
{
	unsigned set;
	char token[CONFIG_SIZE];
	char user[CONFIG_SIZE];
	char proxy[CONFIG_SIZE];
	char title[TITLE_SIZE];
} CONFIG;

int read_config(char *, CONFIG *);
int parse_config(char *, CONFIG *);
void push(char *, CONFIG *);

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
	char *config_name = strtok(buf, " \t\n\r\f\v");
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
			config_value = strtok(NULL, " =");
		}
	}
	return CONFIG_ERROR;
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

	output = curl_easy_escape(curl, msg, 0);
	if (output == NULL)
		goto out;

	asprintf(&opts, "token=%s&user=%s&title=%s&message=%s", config->token, config->user, config->title, output);
	curl_free(output);

	if (opts == NULL)
		goto out;

	curl_easy_setopt(curl, CURLOPT_URL, "https://api.pushover.net/1/messages.json");
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, opts);
	if (config->set & PROXY_SET)
	{
		curl_easy_setopt(curl, CURLOPT_PROXY, config->proxy);
	}

	curl_easy_perform(curl);
out:
	curl_easy_cleanup(curl);
}

int main()
{
	openlog("pushd", LOG_PID | LOG_CONS, LOG_USER);

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

	closelog();
	return 0;
}
