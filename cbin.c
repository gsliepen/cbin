#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>

char *data;
bool cgi;
bool http;

static const char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int header(int code, const char *text) {
	if(http)
		printf("HTTP/1.0 %d %s\nConnection: close\n", code, text);
	else
		printf("Status: %d %s\n", code, text);

	if(code != 200 && code != 303)
		printf("Content-Type: text/html\n\n<title>%d %s</title>\n<h1>%d %s</h1>\n", code, text, code, text);
	
	return code != 200;
}

int form(void) {
	const char *uri = getenv("REQUEST_URI") ?: "/";
	header(200, "OK");
	printf("Content-Type: text/html\n\n");
	printf("<form action=\"%s\" method=\"post\"><p><textarea name=\"text\" maxlength=\"65536\"></textarea></p><input type=\"submit\" value=\"Submit\"></form>\n", uri);
	return 0;
}

FILE *newfile(char **rname) {
	if(!rname)
		return NULL;

	srand(time(NULL) + getpid());

	char name[9];
	char filename[PATH_MAX];

again:
	for(int i = 0; i < 8; i++)
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

int dehex(char c) {
	if (c < 'A')
		return c - '0';
	else if(c < 'a')
		return c - 'A' + 10;
	else
		return c - 'a' + 10;
}

int unescape(char *buf, size_t len) {
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

int post(void) {
	size_t len = atoi(getenv("CONTENT_LENGTH") ?: "0");

	if (len < 6 || len > 65536)
		return header(400, "Bad Request");

	char buf[65536];
	if (fread(buf, len, 1, stdin) != 1 || !strcmp(buf, "text="))
		return header(400, "Bad Request");

	len = unescape(buf + 5, len - 5);
	
	char *name;
	FILE *f = newfile(&name);
	if(!f)
		return header(500, data);

	if(fwrite(buf + 5, len, 1, f) != 1 || fclose(f))
		return header(500, "Internal Server Error");

	header(303, "See Other");
	printf("Location: %s%s%s\n\n", getenv("REQUEST_URI") ?: "/", cgi ? "?" : "", name);

	return 0;
}

bool wellformed(const char *query) {
	return strspn(query, base64) == 8 && !query[8];
}

int cgi_main(void) {
	char *method = getenv("REQUEST_METHOD");
	if (!method)
		errx(1, "REQUEST_METHOD missing");

	char *query = getenv("QUERY_STRING");
	if (!query || !*query) {
		if (!strcmp(method, "GET"))
			return form();
		else if (!strcmp(method, "POST"))
			return post();
		else
			return header(405, "Method Not Allowed");
	}

	if (!wellformed(query))
		return header(400, "Bad Request");

	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", data, query);
	FILE *f = fopen(path, "r");
	if(!f)
		return header(404, "Not Found");

	header(200, "OK");
	printf("Content-Type: text/plain\n\n");
	
	char line[1024];
	while (fgets(line, sizeof line, f))
		fputs(line, stdout);

	return 0;
}

int http_main(void) {
	char request[1024], line[1024], *p;
	if (!fgets(request, sizeof request, stdin))
		err(1, "error reading request");
	while (fgets(line, sizeof line, stdin) && strcspn(line, "\r\n")) {
		if (!strncmp(line, "Content-Length: ", 16))
			setenv("CONTENT_LENGTH", line + 16, true);
	}

	char *sep = strchr(request, ' ');
	if (!sep)
		return header(400, "Bad Request");
	*sep++ = 0;
	setenv("REQUEST_METHOD", request, true);

	p = sep;
	sep = strchr(sep, ' ');
	if (!sep)
		return header(400, "Bad Request");
	*sep++ = 0;

	if (strncmp(sep, "HTTP/1.", 7))
		return header(400, "Bad Request");

	setenv("REQUEST_URI", p, true);

	sep = strrchr(p, '/');

	if (sep)
		setenv("QUERY_STRING", sep + 1, true);
	else
		setenv("QUERY_STRING", p, true);

	cgi_main();
	return 0;
}

int main(int argc, char *argv[]) {
	int opt;

	static const struct option longopts[] = {
		{"cgi", false, 0, 'c'},
		{"data", true, 0, 'd'},
		{"http", false, 0, 'h'},
		{},
	};

	while ((opt = getopt_long(argc, argv, "cd:h", longopts, NULL)) != EOF) {
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
		errx(1, "too many arguments");

	if (cgi && http)
		errx(1, "invalid combination of options");

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
