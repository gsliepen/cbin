/*
    Cbin - a minimalist pastebin
    Copyright (C) 2013  Guus Sliepen <guus@debian.org>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

static char *data;
static bool cgi;
static bool http;
static char *request_method;
static char *request_uri;
static char *query_string;
static size_t content_length;
static char *http_host;
static char *server_port;

static void panic(const char *text) {
	if(errno)
		perror(text);
	else
		fprintf(stderr, "%s\n", text);
	exit(1);
}

static const char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int header(int code, const char *text) {
	if(http)
		printf("HTTP/1.0 %d %s\nConnection: close\n", code, text);
	else
		printf("Status: %d %s\n", code, text);

	if(code != 200 && code != 303)
		printf("Content-Type: text/html\n\n<title>%d %s</title>\n<h1>%d %s</h1>\n", code, text, code, text);
	
	return code != 200;
}

static int form(void) {
	header(200, "OK");
	printf("Content-Type: text/html\n\n");
	printf("<form action=\"%s\" method=\"post\"><p><textarea name=\"text\" cols=\"80\" rows=\"25\" maxlength=\"65536\" required autofocus></textarea></p><input type=\"submit\" value=\"Submit\"></form>\n", request_uri);
	return 0;
}

static FILE *newfile(char **rname) {
	if(!rname)
		return NULL;

	srand(time(NULL) + getpid());

	char name[9];
	char filename[PATH_MAX];
	int i;

again:
	for(i = 0; i < 8; i++)
		name[i] = base64[rand() & 0x3f];
	name[8] = 0;

	snprintf(filename, sizeof filename, "%s/%s", data, name);

	int fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd < 0) {
		if(errno == EEXIST)
			goto again;
		else
			return NULL;
	}

	*rname = strdup(name);
	return fdopen(fd, "w");
}

static int dehex(char c) {
	if (c < 'A')
		return c - '0';
	else if(c < 'a')
		return c - 'A' + 10;
	else
		return c - 'a' + 10;
}

static int unescape(char *buf, size_t len) {
	char *i = buf, *o = buf;
	int newlen = len;

	while (len) {
		if (*i == '%' && len >= 3 && isxdigit(i[1]) && isxdigit(i[2])) {
			*o++ = (dehex(i[1]) << 4) | dehex(i[2]);
			i += 3;
			len -= 3;
			newlen -= 2;
		} else {
			if(*i == '+') {
				*o++ = ' ';
				i++;
			} else {
				*o++ = *i++;
			}
			len--;
		}
	}

	return newlen;
}

static int post(void) {
	if (content_length < 6 || content_length > 65536)
		return header(400, "Bad Request");

	char buf[65536];
	if (fread(buf, content_length, 1, stdin) != 1 || strncmp(buf, "text=", 5) || memchr(buf + 5, '&', content_length - 5))
		return header(400, "Bad Request");

	content_length = unescape(buf + 5, content_length - 5);
	
	char *name;
	FILE *f = newfile(&name);
	if(!f)
		return header(500, "Internal Server Error");

	if(fwrite(buf + 5, content_length, 1, f) != 1 || fclose(f))
		return header(500, "Internal Server Error");

	header(303, "See Other");
	printf("Location: %s%s%s\n", request_uri ?: "/", cgi ? "?" : "", name);
	printf("Content-Type: text/plain\n\n");

	if (http_host) {
		printf("http://%s", http_host);
		if (strcmp(server_port ?: "80", "80"))
			printf(":%s", server_port);
	}
	printf("%s%s%s\n", request_uri ?: "/", cgi ? "?" : "", name);

	return 0;
}

static bool wellformed(const char *query) {
	return strspn(query, base64) == 8 && !query[8];
}

static int common_main(void) {
	if (!request_method)
		panic("REQUEST_METHOD missing");

	if (!query_string || !*query_string) {
		if (!strcmp(request_method, "GET"))
			return form();
		else if (!strcmp(request_method, "POST"))
			return post();
		else
			return header(405, "Method Not Allowed");
	}

	if (!wellformed(query_string) || strcmp(request_method, "GET"))
		return header(400, "Bad Request");

	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", data, query_string);
	FILE *f = fopen(path, "r");
	if (!f)
		return header(404, "Not Found");

	char buf[65536];
	size_t len = fread(buf, 1, sizeof buf, f);
	if (!len)
		return header(500, "Internal Server Error");

	header(200, "OK");
	printf("Content-Type: text/plain\nContent-Length: %lu\n\n", (unsigned long)len);
	
	fwrite(buf, len, 1, stdout);

	return 0;
}

static int cgi_main(void) {
	request_method = getenv("REQUEST_METHOD");
	request_uri = getenv("REQUEST_URI");
	query_string = getenv("QUERY_STRING");
	content_length = atoi(getenv("CONTENT_LENGTH"));
	http_host = getenv("HTTP_HOST");
	server_port = getenv("SERVER_PORT");

	return common_main();
}

static int http_main(void) {
	char request[1024], line[1024], *p;
	if (!fgets(request, sizeof request, stdin))
		panic("error reading request");
	while (fgets(line, sizeof line, stdin) && strcspn(line, "\r\n")) {
		if (!strncmp(line, "Content-Length: ", 16))
			content_length = atoi(line + 16);
	}

	char *sep = strchr(request, ' ');
	if (!sep)
		return header(400, "Bad Request");
	*sep++ = 0;
	request_method = request;

	p = sep;
	sep = strchr(sep, ' ');
	if (!sep)
		return header(400, "Bad Request");
	*sep++ = 0;

	if (strncmp(sep, "HTTP/1.", 7))
		return header(400, "Bad Request");

	request_uri = p;

	sep = strrchr(p, '/');
	query_string = sep ? sep + 1 : p;

	return common_main();
}

int main(int argc, char *argv[]) {
	int opt;

	while ((opt = getopt(argc, argv, "cd:h")) != -1) {
		switch (opt) {
		case 'c':
			cgi = true;
			break;
		case 'd':
			data = strdup(optarg);
			break;
		case 'h':
			http = true;
			break;
		case '?':
			return 1;
		default:
			break;
		}
	}

	if (optind != argc)
		panic("too many arguments");

	if (cgi && http)
		panic("invalid combination of options");

	if (!cgi && !http) {
		if (getenv("GATEWAY_INTERFACE"))
			cgi = true;
		else
			http = true;
	}

	if (!data || !*data) {
		data = getenv("CBIN_DATA");
		if (!data || !*data)
			data = strdup("data");
		else
			data = strdup(data);
	}

	if (data[strlen(data) - 1] == '/')
		data[strlen(data) - 1] = 0;

	if (cgi)
		return cgi_main();

	if (http)
		return http_main();

	return 1;
}
