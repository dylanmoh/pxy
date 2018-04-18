/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "csapp.h"

int main(void) {
    char *buf, *p, *q;
    char arg1[MAXLINE], arg2[MAXLINE], *content;
    int n1=0, n2=0, i;

    /* Extract the two arguments */
    if ((buf = getenv("QUERY_STRING")) != NULL) {
	p = strchr(buf, '&');
	*p = '\0';
	q = strchr(p+1, '&');
	if (q != NULL) {
            *q = '\0';
	}
	strcpy(arg1, buf);
	strcpy(arg2, p+1);
	n1 = atoi(arg1);
	n2 = atoi(arg2);
    }

    content = malloc(n2 + 1);
    for (i = 0; i < n2; i++) {
	    if (i == (n2 / 2)) {
		    sleep(n1);
	    }
	    content[i] = 'a' + (i % 26);
    }
    content[n2] = '\0';

    /* Generate the HTTP response */
    printf("Connection: close\r\n");
    printf("Content-length: %d\r\n", (int)strlen(content));
    printf("Content-type: text/html\r\n\r\n");
    fflush(stdout);
    sleep(n1);
    printf("%s", content);
    fflush(stdout);
    free(content);

    exit(0);
}
/* $end adder */
